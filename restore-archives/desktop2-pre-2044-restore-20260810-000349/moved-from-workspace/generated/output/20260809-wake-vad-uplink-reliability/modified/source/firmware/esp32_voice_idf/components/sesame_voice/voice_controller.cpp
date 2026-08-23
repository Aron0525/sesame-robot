#include "sesame_voice/voice_controller.h"

#include <algorithm>
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
#include "sesame_voice/wake_ack_audio.h"
#include "sesame_voice/wake_capture_policy.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "sesame_voice";
constexpr gpio_num_t kVoiceButton = GPIO_NUM_0;
constexpr uint64_t kMaximumActionDeadlineLeadMs = 5000;
constexpr uint64_t kGatewayConnectDeadlineMs = 15000;

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

bool integer_field_in_range(const cJSON* object, const char* key, int minimum,
                            int maximum, int* output) {
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(value) || value->valuedouble < minimum ||
      value->valuedouble > maximum ||
      value->valuedouble != static_cast<int>(value->valuedouble)) {
    return false;
  }
  if (output != nullptr) *output = static_cast<int>(value->valuedouble);
  return true;
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
  ESP_RETURN_ON_ERROR(wake_vad_.start(), kTag, "initialize WakeNet + VAD");
  ESP_RETURN_ON_ERROR(load_device_config(&config_), kTag,
                      "load device configuration from NVS");

  const gpio_config_t button_config{
      .pin_bit_mask = 1ULL << kVoiceButton,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&button_config), kTag,
                      "configure BOOT voice button");

  downlink_queue_ = xQueueCreate(16, sizeof(DownlinkPacket));
  if (downlink_queue_ == nullptr) return ESP_ERR_NO_MEM;
  gateway_event_queue_ =
      xQueueCreate(kGatewayEventQueueDepth, sizeof(GatewayEvent));
  if (gateway_event_queue_ == nullptr) {
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  uplink_queue_ = xQueueCreate(kUplinkQueueDepth, sizeof(UplinkPacket));
  if (uplink_queue_ == nullptr) {
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  gateway_reconnect_schedule_.reset();
  gateway_connecting_ = false;
  gateway_connect_deadline_ms_ = 0;
  next_gateway_attempt_ms_ = 0;
  running_ = true;
  if (xTaskCreate(uplink_task_entry, "sesame_uplink", 6144, this, 6,
                  &uplink_task_) != pdPASS) {
    running_ = false;
    vQueueDelete(uplink_queue_);
    uplink_queue_ = nullptr;
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreatePinnedToCore(task_entry, "sesame_voice", 12288, this, 7,
                              &task_, 1) != pdPASS) {
    running_ = false;
    if (uplink_task_ != nullptr) {
      vTaskDelete(uplink_task_);
      uplink_task_ = nullptr;
    }
    vQueueDelete(uplink_queue_);
    uplink_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void VoiceController::stop() {
  running_ = false;
  wake_vad_.stop();
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
  if (uplink_task_ != nullptr) {
    for (int attempt = 0; attempt < 250 && uplink_task_ != nullptr; ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (uplink_task_ != nullptr) {
      vTaskDelete(uplink_task_);
      uplink_task_ = nullptr;
    }
  }
  // The voice task is now stopped, so no I2S writer can race this final mute.
  stop_wake_ack();
  if (downlink_queue_ != nullptr) {
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
  }
  if (gateway_event_queue_ != nullptr) {
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
  }
  if (uplink_queue_ != nullptr) {
    vQueueDelete(uplink_queue_);
    uplink_queue_ = nullptr;
  }
  codec_.shutdown();
}

void VoiceController::task_entry(void* context) {
  auto* self = static_cast<VoiceController*>(context);
  self->run();
  self->task_ = nullptr;
  vTaskDelete(nullptr);
}

void VoiceController::uplink_task_entry(void* context) {
  auto* self = static_cast<VoiceController*>(context);
  self->run_uplink();
  self->uplink_task_ = nullptr;
  vTaskDelete(nullptr);
}

void VoiceController::run_uplink() {
  while (running_ || uplink_pending_frames_.load() > 0) {
    UplinkPacket packet{};
    if (uplink_queue_ == nullptr ||
        xQueueReceive(uplink_queue_, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }
    std::array<uint8_t, sesame::protocol::kAudioHeaderSize +
                            sesame::audio::OpusCodec::kMaxPacketBytes>
        message{};
    const sesame::protocol::AudioFrame frame{
        .direction = sesame::protocol::AudioDirection::kUplink,
        .flags = 0,
        .stream_id = 1,
        .generation_id = packet.generation_id,
        .sequence = packet.sequence,
        .timestamp_ms = packet.timestamp_ms,
        .payload = packet.data.data(),
        .payload_size = packet.size,
    };
    size_t message_size = 0;
    if (sesame::protocol::pack_audio_frame(
            frame, message.data(), message.size(), &message_size) ==
            sesame::protocol::AudioFrameError::kOk &&
        gateway_.send_binary(message.data(), message_size) == ESP_OK) {
      ++uplink_sent_frames_;
    } else {
      ++uplink_dropped_frames_;
    }
    --uplink_pending_frames_;
  }
}

void VoiceController::run() {
  while (running_) {
    process_gateway_events();
    const uint64_t timestamp = now_ms();
    maintain_gateway_connection(timestamp);
    const bool pressed = gpio_get_level(kVoiceButton) == 0;
    handle_button(button_.update(pressed, timestamp), timestamp);

    if (wake_ack_active_) {
      // AEC is intentionally not enabled in this change. Keep the microphone
      // off while the local acknowledgement plays, then open the 3-second
      // first-speech window after its final audio frame.
      play_wake_ack_frame(timestamp);
    } else if (should_capture_for_wake(tts_active_)) {
      std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
      if (audio_->read_microphone_frame(pcm.data(), pcm.size(), 100) == ESP_OK) {
        if (wake_vad_.feed_pcm(pcm.data(), pcm.size()) != ESP_OK) {
          ESP_LOGW(kTag, "WakeNet feed failed");
        }
        if (wake_turn_detector_.waiting_for_speech()) {
          buffer_speech_pre_roll(pcm.data(), timestamp);
        }
        if ((button_.recording() && session_ready_) || wake_listening_) {
          capture_and_queue(pcm.data(), timestamp);
        }
        process_wake_vad_signals(timestamp);
      }
    } else {
      // The TTS barge-in transition is represented by VoiceTurnDetector, but
      // remains unwired until AEC provides speech-safe microphone frames.
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
    // BOOT retains priority over every local wake state, including the short
    // acknowledgement and either three-second speech window.
    if (wake_ack_active_) stop_wake_ack();
    if (wake_listening_) {
      wake_listening_ = false;
      finish_listening("wakeword-interrupted");
    }
    wake_turn_detector_.reset();
    clear_speech_pre_roll();
    start_listening(timestamp, "device_button");
  } else if (event == ButtonEvent::kStopRecording ||
             event == ButtonEvent::kMaximumDuration) {
    finish_listening("device_button");
  }
}

void VoiceController::begin_wake_ack() {
  if (audio_ == nullptr || !audio_->initialized()) {
    ESP_LOGW(kTag, "wake acknowledgement unavailable: audio not initialized");
    wake_turn_detector_.reset();
    return;
  }
  wake_ack_offset_samples_ = 0;
  clear_speech_pre_roll();
  wake_ack_active_ = true;
  audio_->set_amplifier_enabled(true);
  ESP_LOGI(kTag, "wake detected: playing local acknowledgement");
}

void VoiceController::stop_wake_ack() {
  wake_ack_active_ = false;
  wake_ack_offset_samples_ = 0;
  if (audio_ != nullptr && audio_->initialized()) {
    std::array<int16_t, sesame::audio::kSamplesPerFrame> silence{};
    audio_->write_speaker_frame(silence.data(), silence.size(), 100);
    audio_->set_amplifier_enabled(false);
  }
}

void VoiceController::play_wake_ack_frame(uint64_t timestamp) {
  if (!wake_ack_active_) return;
  const size_t sample_count = wake_ack_audio_sample_count();
  if (wake_ack_offset_samples_ < sample_count) {
    std::array<int16_t, sesame::audio::kSamplesPerFrame> frame{};
    const size_t samples_to_copy = std::min(
        frame.size(), sample_count - wake_ack_offset_samples_);
    std::memcpy(frame.data(),
                wake_ack_audio_samples() + wake_ack_offset_samples_,
                samples_to_copy * sizeof(frame[0]));
    if (audio_->write_speaker_frame(frame.data(), frame.size(), 100) != ESP_OK) {
      ESP_LOGW(kTag, "wake acknowledgement playback failed");
      stop_wake_ack();
      wake_turn_detector_.reset();
      return;
    }
    wake_ack_offset_samples_ += samples_to_copy;
    // Pace fixed 20-ms I2S frames. This prevents the initial DMA queue from
    // making the first-speech timeout begin before the prompt is audible.
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }

  stop_wake_ack();
  if (!wake_turn_detector_.start_first_speech_wait(timestamp)) {
    ESP_LOGW(kTag, "wake acknowledgement completed outside wake state");
    wake_turn_detector_.reset();
    return;
  }
  ESP_LOGI(kTag, "wake acknowledgement complete: waiting 3000 ms for speech");
}

void VoiceController::process_wake_vad_signals(uint64_t timestamp) {
  WakeVadSignal signal{};
  while (wake_vad_.read_signal(&signal)) {
    const VoiceTurnState prior_state = wake_turn_detector_.state();
    const VoiceTurnEvent event =
        wake_turn_detector_.update(timestamp, signal.wake_detected,
                                   signal.vad_speech);
    if (event == VoiceTurnEvent::kWakeDetected) {
      if (!session_ready_ || button_.recording() || wake_listening_ ||
          turn_state_.state() != sesame::protocol::TurnState::kIdle) {
        wake_turn_detector_.reset();
        continue;
      }
      begin_wake_ack();
    } else if (event == VoiceTurnEvent::kListenStarted) {
      if (!session_ready_ || button_.recording() || wake_listening_ ||
          turn_state_.state() != sesame::protocol::TurnState::kIdle) {
        wake_turn_detector_.reset();
        continue;
      }
      wake_listening_ = true;
      start_listening(timestamp,
                      prior_state == VoiceTurnState::kWaitingForFollowupSpeech
                          ? "tts-followup"
                          : "wakeword");
      flush_speech_pre_roll();
    } else if ((event == VoiceTurnEvent::kListenStopped ||
                event == VoiceTurnEvent::kListenTimedOut) && wake_listening_) {
      wake_listening_ = false;
      finish_listening(event == VoiceTurnEvent::kListenStopped
                           ? "wakeword-vad-endpoint"
                           : "wakeword-max-duration");
    } else if (event == VoiceTurnEvent::kWakeTimedOut ||
               event == VoiceTurnEvent::kFollowupTimedOut) {
      ESP_LOGI(kTag, "%s window expired without speech",
               event == VoiceTurnEvent::kWakeTimedOut ? "wake" : "follow-up");
    }
  }
}

void VoiceController::start_listening(uint64_t timestamp, const char* trigger) {
  if (!session_ready_) return;
  if (tts_active_ ||
      turn_state_.state() == sesame::protocol::TurnState::kThinking ||
      turn_state_.state() == sesame::protocol::TurnState::kSpeaking) {
    copy_identifier(&pending_interrupt_turn_id_, turn_id_.data());
    pending_interrupt_generation_ = active_generation_;
    interrupt_pending_ = pending_interrupt_turn_id_[0] != '\0';
    send_control(sesame::protocol::ControlEventType::kInterrupt, "{}");
    flush_tts();
    if (robot_ != nullptr) robot_->emergency_stop();
    turn_state_.apply(sesame::protocol::TurnEvent::kInterrupted);
  }
  ++turn_counter_;
  std::snprintf(turn_id_.data(), turn_id_.size(), "turn_%08lx_%lu",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(turn_counter_));
  audio_sequence_ = 0;
  uplink_frame_count_ = 0;
  uplink_pending_frames_ = 0;
  uplink_sent_frames_ = 0;
  uplink_dropped_frames_ = 0;
  codec_.reset();
  turn_state_.apply(sesame::protocol::TurnEvent::kButtonPressed);
  std::array<char, 64> payload{};
  const int payload_length = std::snprintf(
      payload.data(), payload.size(), "{\"trigger\":\"%s\"}", trigger);
  if (payload_length > 0 &&
      static_cast<size_t>(payload_length) < payload.size()) {
    send_control(sesame::protocol::ControlEventType::kListenStart,
                 payload.data());
  }
  ESP_LOGI(kTag, "listen start: trigger=%s turn=%lu", trigger,
           static_cast<unsigned long>(turn_counter_));
  (void)timestamp;
}

void VoiceController::finish_listening(const char* trigger) {
  if (!session_ready_ ||
      turn_state_.state() != sesame::protocol::TurnState::kListening) {
    return;
  }
  wait_for_uplink_drain();
  const uint32_t uplink_frames = uplink_sent_frames_.load();
  std::array<char, 96> payload{};
  const int payload_length = std::snprintf(
      payload.data(), payload.size(),
      "{\"reason\":\"%s\",\"uplink_frames\":%lu}", trigger,
      static_cast<unsigned long>(uplink_frames));
  if (payload_length > 0 &&
      static_cast<size_t>(payload_length) < payload.size()) {
    send_control(sesame::protocol::ControlEventType::kListenStop,
                 payload.data());
  }
  turn_state_.apply(sesame::protocol::TurnEvent::kButtonPressed);
  ESP_LOGI(kTag, "listen stop: trigger=%s turn=%lu uplink_frames=%lu", trigger,
           static_cast<unsigned long>(turn_counter_),
           static_cast<unsigned long>(uplink_frames));
}

void VoiceController::capture_and_queue(const int16_t* pcm, uint64_t timestamp) {
  if (pcm == nullptr) return;
  std::array<uint8_t, sesame::audio::OpusCodec::kMaxPacketBytes> opus{};
  size_t opus_size = 0;
  if (codec_.encode(pcm, sesame::audio::kSamplesPerFrame, opus.data(), opus.size(),
                    &opus_size) != ESP_OK) {
    return;
  }
  const UplinkPacket packet{
      .generation_id = turn_counter_,
      .sequence = audio_sequence_++,
      .timestamp_ms = timestamp,
      .size = static_cast<uint16_t>(opus_size),
      .data = opus,
  };
  if (uplink_queue_ != nullptr) {
    ++uplink_pending_frames_;
    if (xQueueSend(uplink_queue_, &packet, 0) != pdTRUE) {
      --uplink_pending_frames_;
      ++uplink_dropped_frames_;
      ESP_LOGW(kTag, "P2 uplink queue full: turn=%lu dropped=%lu",
               static_cast<unsigned long>(turn_counter_),
               static_cast<unsigned long>(uplink_dropped_frames_.load()));
      return;
    }
    ++uplink_frame_count_;
    if (uplink_frame_count_ % 50 == 0) {
      ESP_LOGI(kTag, "P2 uplink queued: turn=%lu frames=%lu",
               static_cast<unsigned long>(turn_counter_),
               static_cast<unsigned long>(uplink_frame_count_));
    }
  } else {
    ++uplink_dropped_frames_;
    ESP_LOGW(kTag, "P2 uplink queue full: turn=%lu dropped=%lu",
             static_cast<unsigned long>(turn_counter_),
             static_cast<unsigned long>(uplink_dropped_frames_.load()));
  }
}

void VoiceController::buffer_speech_pre_roll(const int16_t* pcm,
                                              uint64_t timestamp) {
  if (pcm == nullptr) return;
  SpeechPreRollFrame& frame = speech_pre_roll_[speech_pre_roll_write_index_];
  frame.timestamp_ms = timestamp;
  std::memcpy(frame.pcm.data(), pcm, frame.pcm.size() * sizeof(int16_t));
  speech_pre_roll_write_index_ =
      (speech_pre_roll_write_index_ + 1) % speech_pre_roll_.size();
  if (speech_pre_roll_count_ < speech_pre_roll_.size()) ++speech_pre_roll_count_;
}

void VoiceController::flush_speech_pre_roll() {
  const size_t first =
      (speech_pre_roll_write_index_ + speech_pre_roll_.size() - speech_pre_roll_count_) %
      speech_pre_roll_.size();
  for (size_t offset = 0; offset < speech_pre_roll_count_; ++offset) {
    const SpeechPreRollFrame& frame =
        speech_pre_roll_[(first + offset) % speech_pre_roll_.size()];
    capture_and_queue(frame.pcm.data(), frame.timestamp_ms);
  }
  clear_speech_pre_roll();
}

void VoiceController::clear_speech_pre_roll() {
  speech_pre_roll_count_ = 0;
  speech_pre_roll_write_index_ = 0;
}

void VoiceController::wait_for_uplink_drain() {
  const uint64_t deadline = now_ms() + kUplinkDrainTimeoutMs;
  while (uplink_pending_frames_.load() > 0 && now_ms() < deadline) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (uplink_pending_frames_.load() > 0) {
    ESP_LOGW(kTag, "P2 uplink drain timed out: pending=%lu",
             static_cast<unsigned long>(uplink_pending_frames_.load()));
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
    const char* request_id, bool include_active_turn) {
  std::array<char, 1024> message{};
  const sesame::protocol::ControlEvent event{
      .type = type,
      .session_id = session_id_[0] == '\0' ? nullptr : session_id_.data(),
      .turn_id = include_active_turn && turn_id_[0] != '\0'
                     ? turn_id_.data()
                     : nullptr,
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
  stop_wake_ack();
  wake_turn_detector_.reset();
  wake_listening_ = false;
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
  const bool valid_request_binding =
      operator_control_event
          ? is_valid_identifier_field(root, "request_id", session_id_.size())
          : is_null_field(root, "request_id");
  const bool valid_envelope =
      cJSON_IsObject(root) && is_version_one(root) && type != nullptr &&
      payload != nullptr && has_timestamp(root) && valid_request_binding &&
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
  } else if (operator_control_event && !is_null_field(root, "turn_id")) {
    ESP_LOGW(kTag, "discarded operator control bound to a voice turn");
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
  } else if (operator_control_event) {
    const char* request_id = string_field(root, "request_id");
    const char* kind = string_field(payload, "kind");
    bool accepted = false;
    if (robot_ != nullptr && kind != nullptr) {
      if (std::strcmp(kind, "action") == 0) {
        accepted = robot_->execute_operator_action(string_field(payload, "action"));
      } else if (std::strcmp(kind, "expression") == 0) {
        accepted = robot_->set_operator_expression(
            string_field(payload, "expression"));
      } else if (std::strcmp(kind, "servo") == 0) {
        int servo = 0;
        int angle = 0;
        accepted = integer_field_in_range(payload, "servo", 1, 8, &servo) &&
                   integer_field_in_range(payload, "angle", 0, 180, &angle) &&
                   robot_->set_manual_servo(static_cast<uint8_t>(servo),
                                            static_cast<uint8_t>(angle));
      } else if (std::strcmp(kind, "settings") == 0) {
        int frame_delay_ms = 0;
        int walk_cycles = 0;
        int motor_current_delay_ms = 0;
        accepted =
            integer_field_in_range(payload, "frame_delay_ms", 10, 1000,
                                   &frame_delay_ms) &&
            integer_field_in_range(payload, "walk_cycles", 1, 50,
                                   &walk_cycles) &&
            integer_field_in_range(payload, "motor_current_delay_ms", 0, 500,
                                   &motor_current_delay_ms) &&
            robot_->configure_motion(frame_delay_ms, walk_cycles,
                                     motor_current_delay_ms);
      } else if (std::strcmp(kind, "stop") == 0) {
        robot_->emergency_stop();
        accepted = true;
      }
    }
    send_operator_result(accepted, request_id);
    ESP_LOGI(kTag, "operator.control handled: kind=%s accepted=%s",
             kind == nullptr ? "invalid" : kind, accepted ? "true" : "false");
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
    // `expression.set` and `action.execute` are intentionally not accepted.
    // All motion/expression must be bound to a response.plan for the current
    // BOOT turn and must pass both policy layers.
    ESP_LOGW(kTag, "discarded unsupported gateway control type");
  }
  cJSON_Delete(root);
}

void VoiceController::send_operator_result(bool accepted,
                                           const char* request_id) {
  const char* status = accepted ? "accepted" : "rejected";
  const char* error_code = accepted ? "null" : "\"operator_control_rejected\"";
  std::array<char, 128> result_payload{};
  const int length = std::snprintf(
      result_payload.data(), result_payload.size(),
      "{\"status\":\"%s\",\"error_code\":%s}", status, error_code);
  if (length > 0 && static_cast<size_t>(length) < result_payload.size()) {
    send_control(sesame::protocol::ControlEventType::kActionResult,
                 result_payload.data(), request_id, false);
  }
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
  if (!wake_turn_detector_.start_tts_playback()) {
    ESP_LOGW(kTag, "resetting unexpected local wake state before TTS");
    wake_turn_detector_.reset();
    wake_turn_detector_.start_tts_playback();
  }
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
  if (!interrupt_pending_) {
    turn_id_.fill('\0');
    if (!wake_turn_detector_.start_followup_wait(now_ms())) {
      ESP_LOGW(kTag, "follow-up wait skipped: TTS state was not active");
      wake_turn_detector_.reset();
    }
    ESP_LOGI(kTag,
             "P2 playback completed: generation=%lu; waiting 3000 ms for follow-up",
             static_cast<unsigned long>(generation_id));
  } else {
    ESP_LOGI(kTag, "P2 playback completed: generation=%lu",
             static_cast<unsigned long>(generation_id));
  }
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
