#include "sesame_voice/voice_controller.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "sesame_protocol/audio_frame.h"
#include "sesame_protocol/control_event.h"
#include "sesame_voice/wake_capture_policy.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "sesame_voice";
constexpr gpio_num_t kVoiceButton = GPIO_NUM_0;
constexpr uint64_t kMaximumActionDeadlineLeadMs = 5000;
constexpr uint64_t kGatewayConnectDeadlineMs = 15000;
constexpr uint32_t kWakeAckPlaybackTimeoutMs = 100;
constexpr uint32_t kWakeAckSettleMs = 150;

extern const uint8_t wake_ack_pcm_start[] asm("_binary_wake_ack_pcm_start");
extern const uint8_t wake_ack_pcm_end[] asm("_binary_wake_ack_pcm_end");

uint64_t now_ms() {
  return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

const cJSON* payload_of(const cJSON* root) {
  const cJSON* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
  return cJSON_IsObject(payload) ? payload : nullptr;
}

uint32_t uint_field(const cJSON* object, const char* key) {
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(value) || value->valuedouble < 0 ||
      value->valuedouble > 4294967295.0) {
    return 0;
  }
  return static_cast<uint32_t>(value->valuedouble);
}

uint64_t uint64_field(const cJSON* object, const char* key) {
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(value) || value->valuedouble < 0) {
    return 0;
  }
  return static_cast<uint64_t>(value->valuedouble);
}

const char* string_field(const cJSON* object, const char* key) {
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(value) ? value->valuestring : nullptr;
}

bool is_null_field(const cJSON* object, const char* key) {
  return cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(object, key));
}

bool is_identifier_character(char value) {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '_' || value == '-' ||
         value == '.' || value == ':';
}

bool is_valid_identifier(const char* value, size_t capacity) {
  if (value == nullptr || capacity == 0) return false;
  const size_t length = strnlen(value, capacity);
  if (length == 0 || length >= capacity) return false;
  for (size_t index = 0; index < length; ++index) {
    if (!is_identifier_character(value[index])) return false;
  }
  return true;
}

bool is_valid_identifier_field(const cJSON* object, const char* key,
                               size_t capacity) {
  return object != nullptr &&
         is_valid_identifier(string_field(object, key), capacity);
}

bool is_version_one(const cJSON* root) {
  const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
  return cJSON_IsNumber(version) && version->valuedouble == 1.0;
}

bool has_timestamp(const cJSON* root) {
  const cJSON* timestamp =
      cJSON_GetObjectItemCaseSensitive(root, "timestamp_ms");
  return cJSON_IsNumber(timestamp) && timestamp->valuedouble >= 0 &&
         timestamp->valuedouble <= 9007199254740991.0;
}

template <size_t N>
void copy_identifier(std::array<char, N>* destination, const char* source) {
  if (destination == nullptr || source == nullptr) return;
  std::strncpy(destination->data(), source, destination->size() - 1);
  destination->back() = '\0';
}

bool local_deadline_from_gateway(uint64_t event_timestamp_ms,
                                 uint64_t gateway_deadline_ms,
                                 uint64_t local_now_ms,
                                 uint64_t* local_deadline_ms) {
  if (local_deadline_ms == nullptr || gateway_deadline_ms < event_timestamp_ms) {
    return false;
  }
  const uint64_t remaining_ms = gateway_deadline_ms - event_timestamp_ms;
  if (remaining_ms == 0 || remaining_ms > kMaximumActionDeadlineLeadMs) {
    return false;
  }
  *local_deadline_ms = local_now_ms + remaining_ms;
  return true;
}

}  // namespace

VoiceController::VoiceController(sesame::audio::AudioHal* audio,
                                 sesame::robot::RobotAdapter* robot)
    : audio_(audio), robot_(robot) {}

VoiceController::~VoiceController() { stop(); }

esp_err_t VoiceController::start() {
  if (running_) return ESP_OK;
  if (audio_ == nullptr || !audio_->initialized()) {
    return ESP_ERR_INVALID_STATE;
  }
  ESP_RETURN_ON_ERROR(codec_.initialize(), kTag, "initialize Opus codec");
  ESP_RETURN_ON_ERROR(load_device_config(&config_), kTag,
                      "load device configuration from NVS");
  const esp_err_t wake_vad_result = wake_vad_.start();
  if (wake_vad_result != ESP_OK) {
    codec_.shutdown();
    return wake_vad_result;
  }

  const gpio_config_t button_config{
      .pin_bit_mask = 1ULL << kVoiceButton,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  const esp_err_t button_config_result = gpio_config(&button_config);
  if (button_config_result != ESP_OK) {
    wake_vad_.stop();
    codec_.shutdown();
    return button_config_result;
  }

  downlink_queue_ = xQueueCreate(16, sizeof(DownlinkPacket));
  if (downlink_queue_ == nullptr) {
    wake_vad_.stop();
    codec_.shutdown();
    return ESP_ERR_NO_MEM;
  }
  gateway_event_queue_ =
      xQueueCreate(kGatewayEventQueueDepth, sizeof(GatewayEvent));
  if (gateway_event_queue_ == nullptr) {
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    wake_vad_.stop();
    codec_.shutdown();
    return ESP_ERR_NO_MEM;
  }

  gateway_reconnect_schedule_.reset();
  gateway_connecting_ = false;
  gateway_connect_deadline_ms_ = 0;
  next_gateway_attempt_ms_ = 0;
  running_ = true;
  if (xTaskCreatePinnedToCore(task_entry, "sesame_voice", 12288, this, 7,
                              &task_, 1) != pdPASS) {
    running_ = false;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    wake_vad_.stop();
    codec_.shutdown();
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void VoiceController::stop() {
  running_ = false;
  gateway_.stop();
  if (task_ != nullptr) {
    for (int attempt = 0; attempt < 50 && task_ != nullptr; ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (task_ != nullptr) {
      vTaskDelete(task_);
      task_ = nullptr;
    }
  }
  if (downlink_queue_ != nullptr) {
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
  }
  if (gateway_event_queue_ != nullptr) {
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
  }
  wake_vad_.stop();
  turn_detector_.reset();
  codec_.shutdown();
}

void VoiceController::task_entry(void* context) {
  auto* self = static_cast<VoiceController*>(context);
  self->run();
  self->task_ = nullptr;
  vTaskDelete(nullptr);
}

void VoiceController::run() {
  while (running_) {
    process_gateway_events();
    const uint64_t timestamp = now_ms();
    maintain_gateway_connection(timestamp);
    const bool pressed = gpio_get_level(kVoiceButton) == 0;
    handle_button(button_.update(pressed, timestamp), timestamp);

    if (should_capture_for_wake(tts_active_)) {
      capture_and_process(timestamp);
    } else {
      play_pending_audio();
      complete_tts_if_drained();
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void VoiceController::maintain_gateway_connection(uint64_t timestamp) {
  // `esp_websocket_client_is_connected()` can briefly report false while the
  // client dispatches a just-received control frame. A validated
  // `session.ready` is stronger evidence that the active WSS session is
  // usable. Do not destroy that session merely because of this transient;
  // the actual disconnect callback clears `session_ready_` and schedules the
  // next mDNS/WSS attempt.
  if (session_ready_ || gateway_.connected()) {
    gateway_connecting_ = false;
    gateway_reconnect_schedule_.reset();
    return;
  }

  if (gateway_connecting_) {
    if (timestamp < gateway_connect_deadline_ms_) return;
    ESP_LOGW(kTag, "WSS connection timed out; rediscovering Gateway");
    gateway_.stop();
    gateway_connecting_ = false;
    schedule_gateway_retry(timestamp);
    return;
  }

  if (timestamp < next_gateway_attempt_ms_) return;

  ESP_LOGI(kTag, "discovering Voice Gateway over mDNS");
  const esp_err_t result = gateway_.start(config_, this);
  if (result != ESP_OK) {
    // start() can fail before constructing a WebSocket client. Make every
    // retry begin from a clean observer/client state.
    gateway_.stop();
    ESP_LOGW(kTag, "Gateway discovery or WSS start failed: %s",
             esp_err_to_name(result));
    schedule_gateway_retry(timestamp);
    return;
  }
  gateway_connecting_ = true;
  gateway_connect_deadline_ms_ = timestamp + kGatewayConnectDeadlineMs;
}

void VoiceController::schedule_gateway_retry(uint64_t timestamp) {
  const uint32_t delay_ms = gateway_reconnect_schedule_.next_delay_ms();
  next_gateway_attempt_ms_ = timestamp + delay_ms;
  ESP_LOGI(kTag, "next Gateway discovery in %lu ms",
           static_cast<unsigned long>(delay_ms));
}

void VoiceController::enqueue_gateway_event(GatewayEventKind kind,
                                             const void* data, size_t size) {
  if (!running_.load() || gateway_event_queue_ == nullptr ||
      size > GatewayEvent{}.data.size() || (size > 0 && data == nullptr)) {
    ESP_LOGW(kTag, "discarded invalid gateway callback metadata");
    return;
  }
  GatewayEvent event{};
  event.kind = kind;
  event.size = static_cast<uint16_t>(size);
  if (size > 0) std::memcpy(event.data.data(), data, size);
  if (xQueueSend(gateway_event_queue_, &event, 0) != pdPASS) {
    // Do not process WebSocket callbacks in their producer task. A bounded
    // queue protects the voice task from an untrusted peer flood; loss is
    // safe because control/audio state then fails closed by sequence checks.
    ESP_LOGW(kTag, "gateway callback queue full; event discarded");
  }
}

void VoiceController::process_gateway_events() {
  if (gateway_event_queue_ == nullptr) return;
  GatewayEvent event{};
  // Bound work per tick so a peer cannot starve BOOT polling or I2S service.
  for (int count = 0; count < kGatewayEventsPerTick &&
                      xQueueReceive(gateway_event_queue_, &event, 0) == pdTRUE;
       ++count) {
    process_gateway_event(event);
  }
}

void VoiceController::process_gateway_event(const GatewayEvent& event) {
  switch (event.kind) {
    case GatewayEventKind::kConnected:
      handle_gateway_connected();
      return;
    case GatewayEventKind::kDisconnected:
      handle_gateway_disconnected();
      return;
    case GatewayEventKind::kText:
      process_control_json(reinterpret_cast<const char*>(event.data.data()),
                           event.size);
      return;
    case GatewayEventKind::kBinary:
      process_gateway_binary(event.data.data(), event.size);
      return;
  }
}

void VoiceController::handle_button(ButtonEvent event, uint64_t timestamp) {
  if (!session_ready_) return;
  if (event == ButtonEvent::kStartRecording) {
    handle_voice_turn_event(turn_detector_.start_listening(timestamp),
                            timestamp);
  } else if (event == ButtonEvent::kStopRecording ||
             event == ButtonEvent::kMaximumDuration) {
    if (turn_detector_.listening()) {
      turn_detector_.reset();
      finish_listening("button");
    }
  }
}

void VoiceController::begin_listening(uint64_t timestamp, const char* source) {
  if (!session_ready_) return;
  const auto state = turn_state_.state();
  if (state != sesame::protocol::TurnState::kIdle &&
      state != sesame::protocol::TurnState::kThinking &&
      state != sesame::protocol::TurnState::kSpeaking) {
    return;
  }

  if (state == sesame::protocol::TurnState::kThinking ||
      state == sesame::protocol::TurnState::kSpeaking || tts_active_) {
    if (tts_active_ ||
        state == sesame::protocol::TurnState::kThinking ||
        state == sesame::protocol::TurnState::kSpeaking) {
      copy_identifier(&pending_interrupt_turn_id_, turn_id_.data());
      pending_interrupt_generation_ = active_generation_;
      interrupt_pending_ = pending_interrupt_turn_id_[0] != '\0';
      send_control(sesame::protocol::ControlEventType::kInterrupt, "{}");
      flush_tts();
      if (robot_ != nullptr) robot_->emergency_stop();
      turn_state_.apply(sesame::protocol::TurnEvent::kInterrupted);
    }
  }

  ++turn_counter_;
  std::snprintf(turn_id_.data(), turn_id_.size(), "turn_%08lx_%lu",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(turn_counter_));
  audio_sequence_ = 0;
  uplink_frame_count_ = 0;
  codec_.reset();
  turn_state_.apply(sesame::protocol::TurnEvent::kButtonPressed);
  send_control(sesame::protocol::ControlEventType::kListenStart, "{}");
  ESP_LOGI(kTag, "listen start: source=%s turn=%lu", source,
           static_cast<unsigned long>(turn_counter_));
  (void)timestamp;
}

void VoiceController::finish_listening(const char* source) {
  if (turn_state_.state() != sesame::protocol::TurnState::kListening) return;
  send_control(sesame::protocol::ControlEventType::kListenStop, "{}");
  turn_state_.apply(sesame::protocol::TurnEvent::kButtonPressed);
  ESP_LOGI(kTag, "listen stop: source=%s turn=%lu uplink_frames=%lu", source,
           static_cast<unsigned long>(turn_counter_),
           static_cast<unsigned long>(uplink_frame_count_));
}

void VoiceController::process_wake_vad_signals(uint64_t timestamp) {
  WakeVadSignal signal{};
  while (wake_vad_.read_signal(&signal)) {
    const bool may_wake = session_ready_ &&
                          turn_state_.state() == sesame::protocol::TurnState::kIdle;
    const VoiceTurnEvent event =
        turn_detector_.update(timestamp, may_wake && signal.wake_detected,
                              signal.vad_speech);
    handle_voice_turn_event(event, timestamp);
    if (event == VoiceTurnEvent::kWakeDetected) return;
  }
}

void VoiceController::handle_voice_turn_event(VoiceTurnEvent event,
                                               uint64_t timestamp) {
  switch (event) {
    case VoiceTurnEvent::kWakeDetected:
      ESP_LOGI(kTag, "WakeNet detected: 你好，小智");
      if (play_wake_acknowledgement() != ESP_OK) {
        ESP_LOGW(kTag, "local wake acknowledgement playback failed");
      }
      wake_vad_.discard_pending_signals();
      turn_detector_.begin_waiting_for_speech(now_ms());
      return;
    case VoiceTurnEvent::kListenStarted:
      begin_listening(timestamp, "wake_or_button");
      return;
    case VoiceTurnEvent::kListenStopped:
      finish_listening("vad_silence_2000ms");
      return;
    case VoiceTurnEvent::kWakeTimedOut:
      ESP_LOGI(kTag, "wake timed out before command speech");
      return;
    case VoiceTurnEvent::kListenTimedOut:
      finish_listening("maximum_duration");
      return;
    case VoiceTurnEvent::kNone:
      return;
  }
}

esp_err_t VoiceController::play_wake_acknowledgement() {
  if (audio_ == nullptr || !audio_->initialized()) return ESP_ERR_INVALID_STATE;

  const size_t pcm_bytes = wake_ack_pcm_end - wake_ack_pcm_start;
  if (pcm_bytes == 0 || pcm_bytes % sesame::audio::kPcmBytesPerFrame != 0) {
    return ESP_ERR_INVALID_SIZE;
  }

  esp_err_t result = audio_->set_amplifier_enabled(true);
  std::array<int16_t, sesame::audio::kSamplesPerFrame> frame{};
  for (size_t offset = 0; result == ESP_OK && offset < pcm_bytes;
       offset += sizeof(frame)) {
    std::memcpy(frame.data(), wake_ack_pcm_start + offset, sizeof(frame));
    result = audio_->write_speaker_frame(frame.data(), frame.size(),
                                         kWakeAckPlaybackTimeoutMs);
  }
  audio_->set_amplifier_enabled(false);
  if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(kWakeAckSettleMs));
  return result;
}

void VoiceController::capture_and_process(uint64_t timestamp) {
  std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
  if (audio_->read_microphone_frame(pcm.data(), pcm.size(), 100) != ESP_OK) {
    return;
  }
  if (wake_vad_.feed_pcm(pcm.data(), pcm.size()) != ESP_OK) return;
  process_wake_vad_signals(timestamp);
  if (!turn_detector_.listening()) return;
  send_pcm_frame(pcm.data(), pcm.size(), timestamp);
}

void VoiceController::send_pcm_frame(const int16_t* pcm, size_t samples,
                                     uint64_t timestamp) {
  if (pcm == nullptr || samples != sesame::audio::kSamplesPerFrame) return;
  std::array<uint8_t, sesame::audio::OpusCodec::kMaxPacketBytes> opus{};
  size_t opus_size = 0;
  if (codec_.encode(pcm, samples, opus.data(), opus.size(),
                    &opus_size) != ESP_OK) {
    return;
  }
  std::array<uint8_t, sesame::protocol::kAudioHeaderSize +
                          sesame::audio::OpusCodec::kMaxPacketBytes>
      message{};
  const sesame::protocol::AudioFrame frame{
      .direction = sesame::protocol::AudioDirection::kUplink,
      .flags = 0,
      .stream_id = 1,
      .generation_id = turn_counter_,
      .sequence = audio_sequence_++,
      .timestamp_ms = timestamp,
      .payload = opus.data(),
      .payload_size = opus_size,
  };
  size_t message_size = 0;
  if (sesame::protocol::pack_audio_frame(
          frame, message.data(), message.size(), &message_size) ==
      sesame::protocol::AudioFrameError::kOk) {
    if (gateway_.send_binary(message.data(), message_size) == ESP_OK) {
      ++uplink_frame_count_;
      if (uplink_frame_count_ % 50 == 0) {
        ESP_LOGI(kTag, "P2 uplink progress: turn=%lu frames=%lu",
                 static_cast<unsigned long>(turn_counter_),
                 static_cast<unsigned long>(uplink_frame_count_));
      }
    }
  }
}

void VoiceController::play_pending_audio() {
  if (!tts_active_ || downlink_queue_ == nullptr) return;
  DownlinkPacket packet{};
  if (xQueueReceive(downlink_queue_, &packet, 0) != pdTRUE) return;
  if (packet.generation_id != active_generation_) return;

  std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
  size_t samples = 0;
  if (codec_.decode(packet.data.data(), packet.size, pcm.data(), pcm.size(),
                    &samples) != ESP_OK ||
      samples != pcm.size()) {
    return;
  }
  audio_->write_speaker_frame(pcm.data(), pcm.size(), 100);
}

esp_err_t VoiceController::send_control(
    sesame::protocol::ControlEventType type, const char* payload_json,
    const char* request_id) {
  std::array<char, 1024> message{};
  const sesame::protocol::ControlEvent event{
      .type = type,
      .session_id = session_id_[0] == '\0' ? nullptr : session_id_.data(),
      .turn_id = turn_id_[0] == '\0' ? nullptr : turn_id_.data(),
      .request_id = request_id,
      .sequence = control_sequence_++,
      .timestamp_ms = now_ms(),
      .payload_json = payload_json,
  };
  size_t message_size = 0;
  const auto result = sesame::protocol::serialize_control_event(
      event, message.data(), message.size(), &message_size);
  if (result != sesame::protocol::ControlEventError::kOk) {
    return ESP_ERR_INVALID_ARG;
  }
  return gateway_.send_text(message.data(), message_size);
}

void VoiceController::send_session_hello() {
  std::array<char, 512> payload{};
  const char* conversation = config_.conversation_id[0] == '\0'
                                 ? "null"
                                 : config_.conversation_id.data();
  const int length = config_.conversation_id[0] == '\0'
                         ? std::snprintf(
                               payload.data(), payload.size(),
                               "{\"device_id\":\"%s\",\"gateway_id\":\"%s\","
                               "\"conversation_id\":null,\"protocol_version\":1,"
                               "\"audio\":{\"codec\":\"opus\",\"sample_rate\":"
                               "16000,\"channels\":1,\"frame_duration_ms\":20}}",
                               config_.device_id.data(),
                               config_.gateway_id.data())
                         : std::snprintf(
                               payload.data(), payload.size(),
                               "{\"device_id\":\"%s\",\"gateway_id\":\"%s\","
                               "\"conversation_id\":\"%s\",\"protocol_version\":"
                               "1,\"audio\":{\"codec\":\"opus\",\"sample_rate\":"
                               "16000,\"channels\":1,\"frame_duration_ms\":20}}",
                               config_.device_id.data(),
                               config_.gateway_id.data(), conversation);
  if (length > 0 && static_cast<size_t>(length) < payload.size()) {
    if (send_control(sesame::protocol::ControlEventType::kSessionHello,
                     payload.data()) == ESP_OK) {
      ESP_LOGI(kTag, "P1 session.hello sent: protocol=1 codec=opus rate=16000 frame_ms=20");
    } else {
      ESP_LOGW(kTag, "P1 session.hello could not be queued");
    }
  }
}

void VoiceController::on_gateway_connected() {
  enqueue_gateway_event(GatewayEventKind::kConnected, nullptr, 0);
}

void VoiceController::handle_gateway_connected() {
  gateway_connecting_ = false;
  gateway_reconnect_schedule_.reset();
  session_ready_ = false;
  session_id_.fill('\0');
  turn_id_.fill('\0');
  pending_interrupt_turn_id_.fill('\0');
  interrupt_pending_ = false;
  pending_interrupt_generation_ = 0;
  planned_generation_ = 0;
  active_generation_ = 0;
  expected_downlink_sequence_ = 0;
  control_sequence_ = 0;
  expected_control_sequence_ = 0;
  send_session_hello();
  ESP_LOGI(kTag, "P1 WSS connected; session.hello queued");
}

void VoiceController::on_gateway_disconnected() {
  enqueue_gateway_event(GatewayEventKind::kDisconnected, nullptr, 0);
}

void VoiceController::handle_gateway_disconnected() {
  gateway_connecting_ = false;
  schedule_gateway_retry(now_ms());
  session_ready_ = false;
  button_.reset();
  turn_detector_.reset();
  flush_tts();
  turn_id_.fill('\0');
  pending_interrupt_turn_id_.fill('\0');
  interrupt_pending_ = false;
  pending_interrupt_generation_ = 0;
  if (robot_ != nullptr) robot_->emergency_stop();
  turn_state_.apply(sesame::protocol::TurnEvent::kConnectionLost);
  ESP_LOGW(kTag, "P1 WSS disconnected; audio and motion stopped");
}

void VoiceController::on_gateway_text(const char* data, size_t size) {
  enqueue_gateway_event(GatewayEventKind::kText, data, size);
}

void VoiceController::on_gateway_binary(const uint8_t* data, size_t size) {
  enqueue_gateway_event(GatewayEventKind::kBinary, data, size);
}

void VoiceController::process_gateway_binary(const uint8_t* data, size_t size) {
  sesame::protocol::AudioFrame frame{};
  if (sesame::protocol::unpack_audio_frame(data, size, &frame) !=
          sesame::protocol::AudioFrameError::kOk ||
      frame.direction != sesame::protocol::AudioDirection::kDownlink ||
      frame.flags != 0 || frame.stream_id != 2 || frame.generation_id == 0 ||
      frame.payload_size > sesame::audio::OpusCodec::kMaxPacketBytes ||
      !tts_active_ || frame.generation_id != active_generation_) {
    return;
  }
  if (frame.sequence != expected_downlink_sequence_) {
    flush_tts();
    return;
  }
  ++expected_downlink_sequence_;
  DownlinkPacket packet{
      .generation_id = frame.generation_id,
      .sequence = frame.sequence,
      .size = static_cast<uint16_t>(frame.payload_size),
      .data = {},
  };
  std::memcpy(packet.data.data(), frame.payload, frame.payload_size);
  if (downlink_queue_ == nullptr ||
      xQueueSend(downlink_queue_, &packet, 0) != pdTRUE) {
    flush_tts();
    ESP_LOGW(kTag, "P2 downlink queue overflow; playback flushed");
  }
}

void VoiceController::process_control_json(const char* data, size_t size) {
  if (data == nullptr || size == 0 || size > kMaxGatewayEventBytes ||
      size >= sesame::protocol::kMaxControlFrameBytes) {
    ESP_LOGW(kTag, "discarded oversized gateway control frame");
    return;
  }
  cJSON* root = cJSON_ParseWithLength(data, size);
  if (root == nullptr) {
    ESP_LOGW(kTag, "discarded malformed gateway control frame");
    return;
  }

  const char* type = string_field(root, "type");
  const cJSON* payload = payload_of(root);
  const cJSON* sequence =
      cJSON_GetObjectItemCaseSensitive(root, "sequence");
  const bool operator_control_event =
      type != nullptr && std::strcmp(type, "operator.control") == 0;
  const char* inbound_request_id = string_field(root, "request_id");
  const bool valid_request_id =
      is_null_field(root, "request_id") ||
      (operator_control_event &&
       is_valid_identifier(inbound_request_id, 101));
  const bool valid_envelope =
      cJSON_IsObject(root) && is_version_one(root) && type != nullptr &&
      payload != nullptr && has_timestamp(root) && valid_request_id &&
      cJSON_IsNumber(sequence) && sequence->valuedouble >= 0 &&
      sequence->valuedouble <= 4294967295.0 &&
      static_cast<uint32_t>(sequence->valuedouble) ==
          expected_control_sequence_;
  if (!valid_envelope) {
    ESP_LOGW(kTag, "discarded invalid gateway control envelope");
    cJSON_Delete(root);
    return;
  }

  const bool session_ready_event = std::strcmp(type, "session.ready") == 0;
  const char* inbound_session = string_field(root, "session_id");
  const char* inbound_turn = string_field(root, "turn_id");
  if (session_ready_event) {
    if (session_ready_ || !is_valid_identifier_field(root, "session_id",
                                                      session_id_.size()) ||
        !is_null_field(root, "turn_id")) {
      ESP_LOGW(kTag, "discarded invalid session.ready binding");
      cJSON_Delete(root);
      return;
    }
  } else if (!session_ready_ ||
             !is_valid_identifier(inbound_session, session_id_.size()) ||
             std::strcmp(inbound_session, session_id_.data()) != 0) {
    ESP_LOGW(kTag, "discarded control frame with invalid session binding");
    cJSON_Delete(root);
    return;
  }

  // This frame is now both structurally valid and bound to this WSS session.
  ++expected_control_sequence_;
  const auto matches_active_turn = [this](const char* candidate) {
    return is_valid_identifier(candidate, turn_id_.size()) &&
           turn_id_[0] != '\0' && std::strcmp(candidate, turn_id_.data()) == 0;
  };
  const auto matches_pending_interrupt = [this](const char* candidate) {
    return interrupt_pending_ &&
           is_valid_identifier(candidate, pending_interrupt_turn_id_.size()) &&
           std::strcmp(candidate, pending_interrupt_turn_id_.data()) == 0;
  };

  if (session_ready_event) {
    const char* gateway_id = string_field(payload, "gateway_id");
    const char* conversation = string_field(payload, "conversation_id");
    const cJSON* protocol_version =
        cJSON_GetObjectItemCaseSensitive(payload, "protocol_version");
    const cJSON* audio =
        cJSON_GetObjectItemCaseSensitive(payload, "audio");
    const bool valid_audio =
        cJSON_IsObject(audio) && string_field(audio, "codec") != nullptr &&
        std::strcmp(string_field(audio, "codec"), "opus") == 0 &&
        uint_field(audio, "sample_rate") == 16000 &&
        uint_field(audio, "channels") == 1 &&
        uint_field(audio, "frame_duration_ms") == 20;
    if (gateway_id == nullptr ||
        std::strcmp(gateway_id, config_.gateway_id.data()) != 0 ||
        !is_valid_identifier(conversation, config_.conversation_id.size()) ||
        !cJSON_IsNumber(protocol_version) || protocol_version->valuedouble != 1.0 ||
        !valid_audio) {
      ESP_LOGW(kTag, "discarded invalid session.ready payload");
      cJSON_Delete(root);
      return;
    }

    copy_identifier(&session_id_, inbound_session);
    session_ready_ = true;
    // Persist only after validating the value, then update the in-memory copy
    // used by the next session. Without this second assignment a reconnect in
    // the same boot would keep sending a stale conversation_id.
    if (sesame::transport::save_conversation_id(conversation) == ESP_OK) {
      copy_identifier(&config_.conversation_id, conversation);
    } else {
      ESP_LOGW(kTag, "could not persist validated conversation identifier");
    }
    ESP_LOGI(kTag,
             "P1 session.ready accepted: protocol=1 codec=opus rate=16000 frame_ms=20");
  } else if (operator_control_event && robot_ != nullptr) {
    const char* kind = string_field(payload, "kind");
    const char* request_id = inbound_request_id;
    if (!session_ready_ || inbound_turn != nullptr ||
        !is_valid_identifier(request_id, 101) || kind == nullptr) {
      ESP_LOGW(kTag, "discarded invalid operator control command");
      cJSON_Delete(root);
      return;
    }

    sesame::robot::ActionDecision result =
        sesame::robot::ActionDecision::kUnsafeState;
    if (std::strcmp(kind, "stop") == 0) {
      robot_->emergency_stop();
      result = sesame::robot::ActionDecision::kAllowed;
    } else if (std::strcmp(kind, "action") == 0) {
      const char* action = string_field(payload, "action");
      result = robot_->execute_operator_action(action)
                   ? sesame::robot::ActionDecision::kAllowed
                   : sesame::robot::ActionDecision::kUnsafeState;
    } else if (std::strcmp(kind, "expression") == 0) {
      const char* expression = string_field(payload, "expression");
      result = robot_->set_operator_expression(expression)
                   ? sesame::robot::ActionDecision::kAllowed
                   : sesame::robot::ActionDecision::kUnsafeState;
    } else if (std::strcmp(kind, "servo") == 0) {
      const uint32_t servo = uint_field(payload, "servo");
      const uint32_t angle = uint_field(payload, "angle");
      result = servo >= 1 && servo <= 8 && angle <= 180 &&
                       robot_->set_manual_servo(static_cast<uint8_t>(servo),
                                                static_cast<uint8_t>(angle))
                   ? sesame::robot::ActionDecision::kAllowed
                   : sesame::robot::ActionDecision::kUnsafeState;
    } else if (std::strcmp(kind, "settings") == 0) {
      const uint32_t frame_delay_ms = uint_field(payload, "frame_delay_ms");
      const uint32_t walk_cycles = uint_field(payload, "walk_cycles");
      const uint32_t motor_current_delay_ms =
          uint_field(payload, "motor_current_delay_ms");
      result = frame_delay_ms >= 10 && frame_delay_ms <= 1000 &&
                       walk_cycles >= 1 && walk_cycles <= 50 &&
                       motor_current_delay_ms <= 500 &&
                       robot_->configure_motion(
                           static_cast<int>(frame_delay_ms),
                           static_cast<int>(walk_cycles),
                           static_cast<int>(motor_current_delay_ms))
                   ? sesame::robot::ActionDecision::kAllowed
                   : sesame::robot::ActionDecision::kUnsafeState;
    } else {
      result = sesame::robot::ActionDecision::kUnknownAction;
    }
    send_action_result(result, request_id);
  } else if (std::strcmp(type, "response.plan") == 0 && robot_ != nullptr) {
    const uint32_t generation_id = uint_field(payload, "generation_id");
    const char* expression_id = string_field(payload, "expression_id");
    const uint32_t expression_ttl_ms =
        uint_field(payload, "expression_ttl_ms");
    const char* action_id = string_field(payload, "action_id");
    const char* action_request_id =
        string_field(payload, "action_request_id");
    const bool no_action =
        is_null_field(payload, "action_id") &&
        is_null_field(payload, "action_request_id") &&
        is_null_field(payload, "action_duration_ms") &&
        is_null_field(payload, "action_deadline_ms");
    const bool has_action =
        is_valid_identifier(action_id, 101) &&
        is_valid_identifier(action_request_id, 101) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(
            payload, "action_duration_ms")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(
            payload, "action_deadline_ms"));
    if (!matches_active_turn(inbound_turn) || generation_id == 0 ||
        expression_id == nullptr ||
        turn_state_.state() != sesame::protocol::TurnState::kThinking ||
        (!no_action && !has_action) ||
        sesame::robot::validate_expression(expression_id, expression_ttl_ms) !=
            sesame::robot::ExpressionDecision::kAllowed) {
      ESP_LOGW(kTag, "discarded response.plan outside its active turn");
      cJSON_Delete(root);
      return;
    }

    if (has_action) {
      uint64_t local_deadline_ms = 0;
      if (!local_deadline_from_gateway(
              uint64_field(root, "timestamp_ms"),
              uint64_field(payload, "action_deadline_ms"), now_ms(),
              &local_deadline_ms)) {
        ESP_LOGW(kTag, "discarded response.plan with invalid action deadline");
        cJSON_Delete(root);
        return;
      }
      const sesame::robot::ActionRequest request{
          action_request_id, action_id, uint_field(payload, "action_duration_ms"),
          local_deadline_ms,
      };
      const sesame::robot::ActionDecision decision =
          robot_->execute(request, now_ms());
      send_action_result(decision, action_request_id);
      if (decision != sesame::robot::ActionDecision::kAllowed) {
        ESP_LOGW(kTag, "P2 action rejected by local policy");
        cJSON_Delete(root);
        return;
      }
    }

    if (robot_->set_expression(expression_id, expression_ttl_ms) !=
        sesame::robot::ExpressionDecision::kAllowed) {
      ESP_LOGW(kTag, "P2 expression rejected by local policy");
      cJSON_Delete(root);
      return;
    }
    planned_generation_ = generation_id;
    ESP_LOGI(kTag, "P2 response.plan accepted: generation=%lu action=%s expression=%s",
             static_cast<unsigned long>(generation_id),
             has_action ? action_id : "none", expression_id);
  } else if (std::strcmp(type, "tts.start") == 0) {
    const uint32_t generation_id = uint_field(payload, "generation_id");
    if (!matches_active_turn(inbound_turn) || generation_id == 0 ||
        generation_id != planned_generation_) {
      ESP_LOGW(kTag, "discarded tts.start with invalid generation binding");
      cJSON_Delete(root);
      return;
    }
    planned_generation_ = 0;
    begin_tts(generation_id);
  } else if (std::strcmp(type, "tts.stop") == 0) {
    const uint32_t generation_id = uint_field(payload, "generation_id");
    if (!matches_active_turn(inbound_turn) || generation_id == 0 ||
        generation_id != active_generation_) {
      ESP_LOGW(kTag, "discarded tts.stop with invalid generation binding");
      cJSON_Delete(root);
      return;
    }
    finish_tts(generation_id);
  } else if (std::strcmp(type, "tts.flush") == 0) {
    const uint32_t generation_id = uint_field(payload, "generation_id");
    if (matches_pending_interrupt(inbound_turn)) {
      if (generation_id != pending_interrupt_generation_) {
        ESP_LOGW(kTag, "discarded stale tts.flush after interrupt");
        cJSON_Delete(root);
        return;
      }
      interrupt_pending_ = false;
      pending_interrupt_generation_ = 0;
      pending_interrupt_turn_id_.fill('\0');
      ESP_LOGI(kTag, "P2 interrupt confirmed by gateway");
    } else if (matches_active_turn(inbound_turn)) {
      if (generation_id != active_generation_) {
        ESP_LOGW(kTag, "discarded tts.flush with invalid generation binding");
        cJSON_Delete(root);
        return;
      }
      flush_tts();
      if (robot_ != nullptr) robot_->emergency_stop();
      turn_state_.apply(sesame::protocol::TurnEvent::kInterrupted);
      ESP_LOGI(kTag, "P2 playback flushed by gateway");
    } else {
      ESP_LOGW(kTag, "discarded tts.flush outside its active turn");
      cJSON_Delete(root);
      return;
    }
  } else if (std::strcmp(type, "error") == 0) {
    if (!matches_active_turn(inbound_turn)) {
      ESP_LOGW(kTag, "discarded error outside its active turn");
      cJSON_Delete(root);
      return;
    }
    flush_tts();
    turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
    turn_id_.fill('\0');
    ESP_LOGW(kTag, "P2 gateway reported a safe processing failure");
  } else {
    // Voice actions remain bound to response.plan. Only an authenticated
    // operator.control event has an out-of-turn execution path.
    ESP_LOGW(kTag, "discarded unsupported gateway control type");
  }
  cJSON_Delete(root);
}

void VoiceController::send_action_result(
    sesame::robot::ActionDecision decision, const char* request_id) {
  const char* status = "rejected";
  const char* error_code = "\"action_rejected\"";
  if (decision == sesame::robot::ActionDecision::kAllowed) {
    status = "accepted";
    error_code = "null";
  } else if (decision == sesame::robot::ActionDecision::kExpired) {
    status = "expired";
    error_code = "\"deadline_expired\"";
  } else if (decision == sesame::robot::ActionDecision::kInvalidRequestId) {
    error_code = "\"invalid_request_id\"";
  } else if (decision == sesame::robot::ActionDecision::kUnknownAction) {
    error_code = "\"unknown_action\"";
  } else if (decision == sesame::robot::ActionDecision::kDurationOutOfRange) {
    error_code = "\"duration_out_of_range\"";
  } else if (decision == sesame::robot::ActionDecision::kUnsafeState) {
    error_code = "\"unsafe_state\"";
  }
  std::array<char, 128> result_payload{};
  const int length = std::snprintf(
      result_payload.data(), result_payload.size(),
      "{\"status\":\"%s\",\"error_code\":%s}", status, error_code);
  if (length > 0 && static_cast<size_t>(length) < result_payload.size()) {
    send_control(sesame::protocol::ControlEventType::kActionResult,
                 result_payload.data(), request_id);
    ESP_LOGI(kTag, "P2 action.result sent: status=%s", status);
  }
}

void VoiceController::begin_tts(uint32_t generation_id) {
  if (generation_id == 0) return;
  flush_tts();
  if (!turn_state_.start_generation(generation_id)) return;
  active_generation_ = generation_id;
  expected_downlink_sequence_ = 0;
  tts_stop_requested_ = false;
  codec_.reset();
  std::array<int16_t, sesame::audio::kSamplesPerFrame> silence{};
  audio_->write_speaker_frame(silence.data(), silence.size(), 100);
  audio_->set_amplifier_enabled(true);
  tts_active_ = true;
  ESP_LOGI(kTag, "P2 tts.start accepted: generation=%lu",
           static_cast<unsigned long>(generation_id));
}

void VoiceController::finish_tts(uint32_t generation_id) {
  if (!tts_active_ || generation_id != active_generation_) return;
  tts_stop_requested_ = true;
  ESP_LOGI(kTag, "P2 tts.stop received: generation=%lu",
           static_cast<unsigned long>(generation_id));
}

void VoiceController::complete_tts_if_drained() {
  if (!tts_active_ || !tts_stop_requested_ ||
      (downlink_queue_ != nullptr &&
       uxQueueMessagesWaiting(downlink_queue_) > 0)) {
    return;
  }
  const uint32_t generation_id = active_generation_;
  std::array<int16_t, sesame::audio::kSamplesPerFrame> silence{};
  audio_->write_speaker_frame(silence.data(), silence.size(), 100);
  audio_->set_amplifier_enabled(false);
  tts_active_ = false;
  tts_stop_requested_ = false;
  turn_state_.stop_generation(generation_id);
  if (!interrupt_pending_) turn_id_.fill('\0');
  ESP_LOGI(kTag, "P2 playback completed: generation=%lu",
           static_cast<unsigned long>(generation_id));
}

void VoiceController::flush_tts() {
  if (downlink_queue_ != nullptr) xQueueReset(downlink_queue_);
  if (audio_ != nullptr && audio_->initialized()) {
    audio_->set_amplifier_enabled(false);
  }
  tts_active_ = false;
  tts_stop_requested_ = false;
  planned_generation_ = 0;
  active_generation_ = 0;
  expected_downlink_sequence_ = 0;
}

}  // namespace sesame::voice
