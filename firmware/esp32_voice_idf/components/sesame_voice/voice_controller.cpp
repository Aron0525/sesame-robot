#include "sesame_voice/voice_controller.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <new>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"

#include "sesame_protocol/audio_frame.h"
#include "sesame_protocol/control_event.h"
#include "sesame_voice/wake_ack_audio.h"
#include "sesame_voice/wake_capture_policy.h"
#include "owner_voiceprint_template.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "sesame_voice";
constexpr gpio_num_t kVoiceButton = GPIO_NUM_0;
constexpr uint64_t kMaximumActionDeadlineLeadMs = 5000;
constexpr uint64_t kGatewayConnectDeadlineMs = 15000;
constexpr uint32_t kMaximumRecordingDurationMs = 10000;
constexpr char kManualListenStartPayload[] = R"({"trigger":"manual"})";
constexpr char kWakewordListenStartPayload[] = R"({"trigger":"wakeword"})";
constexpr char kFollowupListenStartPayload[] = R"({"trigger":"followup"})";

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
  ESP_RETURN_ON_ERROR(wake_vad_.start(), kTag, "initialize wake word + VAD");
  ESP_RETURN_ON_ERROR(load_device_config(&config_), kTag,
                      "load device configuration from NVS");
  uint8_t stored_wake_threshold = 20;
  ESP_RETURN_ON_ERROR(
      sesame::transport::load_wake_threshold_hundredths(
          &stored_wake_threshold),
      kTag, "load wake threshold from NVS");
  // set_detection_threshold_hundredths() returns bool, not esp_err_t. Passing
  // true to ESP_RETURN_ON_ERROR would treat the successful value 1 as an
  // error, aborting the complete voice runtime before Wi-Fi/WSS can start.
  if (!wake_vad_.set_detection_threshold_hundredths(stored_wake_threshold)) {
    ESP_LOGW(kTag,
             "stored wake threshold is invalid; falling back to default 0.20");
    if (!wake_vad_.set_detection_threshold_hundredths(20)) {
      return ESP_ERR_INVALID_ARG;
    }
    stored_wake_threshold = 20;
  }
  ESP_LOGI(kTag, "wake threshold restored: %.2f",
           stored_wake_threshold / 100.0f);

  const gpio_config_t button_config{
      .pin_bit_mask = 1ULL << kVoiceButton,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&button_config), kTag,
                      "configure BOOT voice button");
  ESP_LOGI(kTag,
           "BOOT recording toggle ready: debounce=80 ms, maximum=10000 ms");

  ESP_LOGI(kTag,
           "voice allocation before queues: internal free=%lu largest=%lu; PSRAM free=%lu; "
           "downlink=%u x 16; gateway=%u x %u; outbound=%u x %u",
           static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
           static_cast<unsigned>(sizeof(DownlinkPacket)),
           static_cast<unsigned>(sizeof(GatewayEvent)),
           static_cast<unsigned>(kGatewayEventQueueDepth),
           static_cast<unsigned>(sizeof(OutboundFrame)),
           static_cast<unsigned>(kOutboundQueueDepth));

  downlink_queue_ = xQueueCreate(16, sizeof(DownlinkPacket));
  if (downlink_queue_ == nullptr) {
    ESP_LOGE(kTag, "allocate downlink queue in internal RAM");
    return ESP_ERR_NO_MEM;
  }
  gateway_event_queue_ = xQueueCreate(kGatewayEventQueueDepth,
                                      sizeof(GatewayEvent));
  if (gateway_event_queue_ == nullptr) {
    ESP_LOGE(kTag, "allocate gateway event queue in internal RAM");
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  outbound_queue_ = xQueueCreate(kOutboundQueueDepth, sizeof(OutboundFrame));
  if (outbound_queue_ == nullptr) {
    ESP_LOGE(kTag, "allocate outbound queue in internal RAM");
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }

  const esp_err_t store_result = conversation_store_.start();
  if (store_result != ESP_OK) {
    vQueueDelete(outbound_queue_);
    outbound_queue_ = nullptr;
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return store_result;
  }
  const esp_err_t wake_threshold_store_result = wake_threshold_store_.start();
  if (wake_threshold_store_result != ESP_OK) {
    conversation_store_.stop();
    vQueueDelete(outbound_queue_);
    outbound_queue_ = nullptr;
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return wake_threshold_store_result;
  }

  void* preroll_storage = heap_caps_calloc(
      1, sizeof(PcmPreRollBuffer), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (preroll_storage == nullptr) {
    wake_threshold_store_.stop();
    conversation_store_.stop();
    vQueueDelete(outbound_queue_);
    outbound_queue_ = nullptr;
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  pcm_preroll_ = new (preroll_storage) PcmPreRollBuffer();

  void* owner_voice_gate_storage = heap_caps_calloc(
      1, sizeof(OwnerVoiceGate), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (owner_voice_gate_storage == nullptr) {
    pcm_preroll_->~PcmPreRollBuffer();
    heap_caps_free(pcm_preroll_);
    pcm_preroll_ = nullptr;
    wake_threshold_store_.stop();
    conversation_store_.stop();
    vQueueDelete(outbound_queue_);
    outbound_queue_ = nullptr;
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  owner_voice_gate_ = new (owner_voice_gate_storage)
      OwnerVoiceGate(owner_voiceprint::kTemplate);
  ESP_LOGI(kTag, "owner voice gate loaded: enrolled_samples=%d threshold=%.3f",
           owner_voiceprint::kEnrollmentSampleCount,
           owner_voiceprint::kTemplate.threshold);

  gateway_reconnect_schedule_.reset();
  gateway_connection_.disconnected();
  gateway_connect_deadline_ms_ = 0;
  next_gateway_attempt_ms_ = 0;
  running_ = true;
  if (xTaskCreatePinnedToCore(outbound_task_entry, "sesame_uplink",
                              kOutboundTaskStackBytes, this, 5,
                              &outbound_task_, 0) != pdPASS) {
    running_ = false;
    wake_threshold_store_.stop();
    conversation_store_.stop();
    owner_voice_gate_->~OwnerVoiceGate();
    heap_caps_free(owner_voice_gate_);
    owner_voice_gate_ = nullptr;
    pcm_preroll_->~PcmPreRollBuffer();
    heap_caps_free(pcm_preroll_);
    pcm_preroll_ = nullptr;
    vQueueDelete(outbound_queue_);
    outbound_queue_ = nullptr;
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreatePinnedToCoreWithCaps(
          task_entry, "sesame_voice", kVoiceTaskStackBytes, this, 7, &task_,
          1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    running_ = false;
    wake_threshold_store_.stop();
    conversation_store_.stop();
    owner_voice_gate_->~OwnerVoiceGate();
    heap_caps_free(owner_voice_gate_);
    owner_voice_gate_ = nullptr;
    pcm_preroll_->~PcmPreRollBuffer();
    heap_caps_free(pcm_preroll_);
    pcm_preroll_ = nullptr;
    if (outbound_task_ != nullptr) {
      vTaskDelete(outbound_task_);
      outbound_task_ = nullptr;
    }
    vQueueDelete(outbound_queue_);
    outbound_queue_ = nullptr;
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
  if (task_ != nullptr) {
    for (int attempt = 0; attempt < 50 && task_ != nullptr; ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (task_ != nullptr) {
      vTaskDeleteWithCaps(task_);
      task_ = nullptr;
    }
  }
  if (outbound_task_ != nullptr) {
    for (int attempt = 0; attempt < 50 && outbound_task_ != nullptr;
         ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (outbound_task_ != nullptr) {
      vTaskDelete(outbound_task_);
      outbound_task_ = nullptr;
    }
  }
  gateway_.stop();
  wake_threshold_store_.stop();
  conversation_store_.stop();
  if (owner_voice_gate_ != nullptr) {
    owner_voice_gate_->~OwnerVoiceGate();
    heap_caps_free(owner_voice_gate_);
    owner_voice_gate_ = nullptr;
  }
  if (pcm_preroll_ != nullptr) {
    pcm_preroll_->~PcmPreRollBuffer();
    heap_caps_free(pcm_preroll_);
    pcm_preroll_ = nullptr;
  }
  // The voice task is stopped, so no I2S writer can race this final mute.
  stop_wake_ack();
  if (downlink_queue_ != nullptr) {
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
  }
  if (gateway_event_queue_ != nullptr) {
    vQueueDelete(gateway_event_queue_);
    gateway_event_queue_ = nullptr;
  }
  if (outbound_queue_ != nullptr) {
    vQueueDelete(outbound_queue_);
    outbound_queue_ = nullptr;
  }
  codec_.shutdown();
}

void VoiceController::task_entry(void* context) {
  auto* self = static_cast<VoiceController*>(context);
  self->run();
  self->task_ = nullptr;
  vTaskDeleteWithCaps(nullptr);
}

void VoiceController::outbound_task_entry(void* context) {
  auto* self = static_cast<VoiceController*>(context);
  self->outbound_loop();
  self->outbound_task_ = nullptr;
  vTaskDelete(nullptr);
}

void VoiceController::outbound_loop() {
  while (running_) {
    OutboundFrame& frame = outbound_work_frame_;
    frame = {};
    if (outbound_queue_ == nullptr ||
        xQueueReceive(outbound_queue_, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }
    if (!running_ ||
        frame.connection_epoch != outbound_connection_epoch_.load()) {
      continue;
    }

    const esp_err_t result = frame.kind == OutboundFrameKind::kText
                                 ? gateway_.send_text(
                                       reinterpret_cast<const char*>(frame.data.data()),
                                       frame.size, 2000)
                                 : gateway_.send_binary(frame.data.data(), frame.size,
                                                        2000);
    if (result != ESP_OK) {
      ESP_LOGW(kTag, "outbound WSS frame dropped: kind=%s result=%s",
               frame.kind == OutboundFrameKind::kText ? "text" : "binary",
               esp_err_to_name(result));
      // A lost control frame or a sequence-bearing audio frame makes the
      // current protocol session unrecoverable. Reconnect instead of sending
      // later frames into a guaranteed sequence error.
      transport_fault_requested_ = true;
    } else if (frame.kind == OutboundFrameKind::kBinary &&
               first_uplink_pending_.exchange(false)) {
      ESP_LOGI(kTag, "first uplink frame sent; sender stack free=%u bytes",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
  }
}

void VoiceController::run() {
  while (running_) {
    process_gateway_events();
    const uint64_t timestamp = now_ms();
    if (downlink_fault_requested_.exchange(false)) {
      fail_tts_playback("downlink queue overflow");
    }
    if (transport_fault_requested_.exchange(false)) {
      ESP_LOGW(kTag, "WSS send failed; resetting protocol session");
      gateway_.stop();
      handle_gateway_disconnected();
    }
    maintain_gateway_connection(timestamp);
    const bool pressed = gpio_get_level(kVoiceButton) == 0;
    handle_button(button_.update(pressed, timestamp), timestamp);

    const VoiceTurnState local_voice_state = wake_turn_detector_.state();
    const bool idle_wake =
        !tts_active_ &&
        turn_state_.state() == sesame::protocol::TurnState::kIdle &&
        local_voice_state == VoiceTurnState::kIdleWakeListening;
    const bool wake_should_be_armed =
        session_ready_ && !wake_ack_active_ && !capture_session_.active() &&
        idle_wake;
    wake_vad_.set_wake_enabled(wake_should_be_armed);
    if (!wake_should_be_armed && owner_voice_gate_ != nullptr) {
      owner_voice_gate_->clear();
    }

    if (wake_ack_active_) {
      // No AEC is active. Keep the microphone quiet during the local prompt,
      // then start the three-second speech window only after it is audible.
      play_wake_ack_frame(timestamp);
    } else if (session_ready_ && !tts_active_) {
      std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
      if (audio_->read_microphone_frame(pcm.data(), pcm.size(), 100) == ESP_OK) {
        const VoiceTurnState state_before_feed = wake_turn_detector_.state();
        const bool waiting_for_speech =
            state_before_feed == VoiceTurnState::kWaitingForFirstSpeech ||
            state_before_feed == VoiceTurnState::kWaitingForFollowupSpeech;
        if (pcm_preroll_ != nullptr &&
            (capture_session_.active() || waiting_for_speech)) {
          pcm_preroll_->push(pcm.data(), pcm.size(), timestamp);
        }
        if (capture_session_.active()) {
          // Add one live frame and consume up to two, catching up the 500-ms
          // pre-roll without interrupting the 20-ms microphone cadence.
          drain_pcm_uplink(2);
        }
        if (wake_should_be_armed && owner_voice_gate_ != nullptr) {
          owner_voice_gate_->feed_pcm(pcm.data(), pcm.size());
        }
        if (should_capture_for_wake(tts_active_.load())) {
          wake_vad_.feed_pcm(pcm.data(), pcm.size());
        }
        process_wake_vad_signals(timestamp);
      }
    }
    if (capture_session_.expired(timestamp, kMaximumRecordingDurationMs)) {
      finish_listening("maximum-duration");
    }
    play_pending_audio();
    complete_tts_if_drained();
    if (!session_ready_) vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void VoiceController::maintain_gateway_connection(uint64_t timestamp) {
  if (session_ready_) {
    gateway_connection_.session_ready();
    gateway_reconnect_schedule_.reset();
    return;
  }

  if (gateway_connection_.awaiting_connection()) {
    if (timestamp < gateway_connect_deadline_ms_) return;
    ESP_LOGW(kTag, "WSS connection timed out; rediscovering Gateway");
    gateway_.stop();
    gateway_connection_.disconnected();
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
  gateway_connection_.start_attempt();
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
      size > kMaxGatewayEventBytes || (size > 0 && data == nullptr)) {
    ESP_LOGW(kTag, "discarded invalid gateway callback metadata");
    return;
  }
  GatewayEvent& event = gateway_event_work_;
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
  }
}

void VoiceController::handle_button(ButtonEvent event, uint64_t timestamp) {
  if (!session_ready_ || event != ButtonEvent::kPressed) return;
  if (capture_session_.active()) {
    finish_listening("device_button");
    return;
  }
  // BOOT is a peer wake source. It cancels a pending local acknowledgement,
  // then enters the same capture lifecycle as a spoken wake word.
  if (wake_ack_active_) stop_wake_ack();
  if (pcm_preroll_ != nullptr) pcm_preroll_->clear();
  wake_turn_detector_.reset();
  start_listening(timestamp, CaptureSource::kManual);
}

void VoiceController::begin_wake_ack() {
  if (audio_ == nullptr || !audio_->initialized()) {
    ESP_LOGW(kTag, "wake acknowledgement unavailable: audio not initialized");
    wake_turn_detector_.reset();
    return;
  }
  wake_ack_offset_samples_ = 0;
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
    const size_t samples_to_copy =
        std::min(frame.size(), sample_count - wake_ack_offset_samples_);
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
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }

  stop_wake_ack();
  if (pcm_preroll_ != nullptr) pcm_preroll_->clear();
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
      if (!session_ready_ || capture_session_.active() ||
          turn_state_.state() != sesame::protocol::TurnState::kIdle) {
        wake_turn_detector_.reset();
        continue;
      }
      if (owner_voice_gate_ == nullptr) {
        ESP_LOGE(kTag, "owner voice gate unavailable; rejecting spoken wake");
        wake_turn_detector_.reset();
        continue;
      }
      const OwnerVoiceprintDecision decision = owner_voice_gate_->verify_latest();
      owner_voice_gate_->clear();
      if (!decision.accepted) {
        ESP_LOGI(kTag,
                 "spoken wake rejected by owner voice gate: template=%d audio=%d score=%.3f",
                 decision.template_available, decision.has_sufficient_audio,
                 decision.score);
        wake_turn_detector_.reset();
        continue;
      }
      ESP_LOGI(kTag, "spoken wake accepted by owner voice gate: score=%.3f",
               decision.score);
      begin_wake_ack();
    } else if (event == VoiceTurnEvent::kListenStarted) {
      if (!session_ready_ || capture_session_.active() ||
          turn_state_.state() != sesame::protocol::TurnState::kIdle) {
        wake_turn_detector_.reset();
        continue;
      }
      const CaptureSource source =
          prior_state == VoiceTurnState::kWaitingForFollowupSpeech
              ? CaptureSource::kFollowup
              : CaptureSource::kWakeword;
      if (!start_listening(timestamp, source)) {
        wake_turn_detector_.reset();
      }
    } else if ((event == VoiceTurnEvent::kListenStopped ||
                event == VoiceTurnEvent::kListenTimedOut) &&
               (capture_session_.source() == CaptureSource::kWakeword ||
                capture_session_.source() == CaptureSource::kFollowup)) {
      finish_listening(event == VoiceTurnEvent::kListenStopped
                           ? "vad-endpoint"
                           : "vad-max-duration");
    } else if (event == VoiceTurnEvent::kWakeTimedOut) {
      if (pcm_preroll_ != nullptr) pcm_preroll_->clear();
      ESP_LOGI(kTag, "wake window expired without speech");
    } else if (event == VoiceTurnEvent::kFollowupTimedOut) {
      if (pcm_preroll_ != nullptr) pcm_preroll_->clear();
      ESP_LOGI(kTag, "follow-up window expired; waiting for wake word");
    }
  }
}

bool VoiceController::start_listening(uint64_t timestamp,
                                      CaptureSource source) {
  if (!session_ready_ || capture_session_.active() ||
      source == CaptureSource::kNone) {
    return false;
  }
  if (tts_active_ ||
      turn_state_.state() == sesame::protocol::TurnState::kThinking ||
      turn_state_.state() == sesame::protocol::TurnState::kSpeaking) {
    copy_identifier(&pending_interrupt_turn_id_, turn_id_.data());
    pending_interrupt_generation_ = active_generation_;
    interrupt_pending_ = pending_interrupt_turn_id_[0] != '\0';
    if (send_control(sesame::protocol::ControlEventType::kInterrupt, "{}") !=
        ESP_OK) {
      transport_fault_requested_ = true;
      return false;
    }
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
  if (codec_.reset() != ESP_OK ||
      !turn_state_.apply(sesame::protocol::TurnEvent::kButtonPressed)) {
    turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
    turn_id_.fill('\0');
    return false;
  }
  const char* payload = source == CaptureSource::kManual
                            ? kManualListenStartPayload
                        : source == CaptureSource::kFollowup
                            ? kFollowupListenStartPayload
                            : kWakewordListenStartPayload;
  const uint32_t preroll_duration_ms =
      source != CaptureSource::kManual && pcm_preroll_ != nullptr
          ? pcm_preroll_->duration_ms()
          : 0;
  const uint64_t capture_started_ms =
      timestamp >= preroll_duration_ms ? timestamp - preroll_duration_ms : 0;
  if (send_control(sesame::protocol::ControlEventType::kListenStart, payload) !=
          ESP_OK ||
      !capture_session_.start(source, capture_started_ms)) {
    turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
    turn_id_.fill('\0');
    transport_fault_requested_ = true;
    return false;
  }
  first_uplink_pending_ = true;
  const char* trigger = source == CaptureSource::kManual
                            ? "manual"
                        : source == CaptureSource::kFollowup ? "followup"
                                                             : "wakeword";
  ESP_LOGI(kTag, "listen start: trigger=%s turn=%lu", trigger,
           static_cast<unsigned long>(turn_counter_));
  return true;
}

void VoiceController::finish_listening(const char* trigger) {
  if (pcm_preroll_ != nullptr) {
    drain_pcm_uplink(PcmPreRollBuffer::kCapacityFrames);
    pcm_preroll_->clear();
  }
  if (!capture_session_.stop()) return;
  first_uplink_pending_ = false;
  if (!session_ready_ ||
      turn_state_.state() != sesame::protocol::TurnState::kListening) {
    turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
    return;
  }
  if (send_control(sesame::protocol::ControlEventType::kListenStop, "{}") !=
      ESP_OK) {
    turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
    transport_fault_requested_ = true;
    return;
  }
  turn_state_.apply(sesame::protocol::TurnEvent::kButtonPressed);
  ESP_LOGI(kTag, "listen stop: trigger=%s turn=%lu uplink_frames=%lu", trigger,
           static_cast<unsigned long>(turn_counter_),
           static_cast<unsigned long>(uplink_frame_count_));
}

void VoiceController::drain_pcm_uplink(size_t maximum_frames) {
  if (!capture_session_.active() || pcm_preroll_ == nullptr) return;
  PcmPreRollBuffer::Frame frame{};
  for (size_t count = 0;
       count < maximum_frames && pcm_preroll_->pop_oldest(&frame); ++count) {
    capture_and_send(frame.samples.data(), frame.timestamp_ms);
  }
}

void VoiceController::capture_and_send(const int16_t* pcm, uint64_t timestamp) {
  if (pcm == nullptr) return;
  std::array<uint8_t, sesame::audio::OpusCodec::kMaxPacketBytes> opus{};
  size_t opus_size = 0;
  if (codec_.encode(pcm, sesame::audio::kSamplesPerFrame, opus.data(), opus.size(),
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
      .sequence = audio_sequence_,
      .timestamp_ms = timestamp,
      .payload = opus.data(),
      .payload_size = opus_size,
  };
  size_t message_size = 0;
  if (sesame::protocol::pack_audio_frame(
          frame, message.data(), message.size(), &message_size) ==
      sesame::protocol::AudioFrameError::kOk) {
    if (enqueue_outbound_binary(message.data(), message_size) == ESP_OK) {
      ++audio_sequence_;
      ++uplink_frame_count_;
      if (uplink_frame_count_ % 50 == 0) {
        ESP_LOGI(kTag,
                 "P2 uplink progress: turn=%lu frames=%lu voice stack free=%u bytes",
                 static_cast<unsigned long>(turn_counter_),
                 static_cast<unsigned long>(uplink_frame_count_),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
      }
    } else {
      ESP_LOGW(kTag, "uplink queue full; dropping 20-ms audio frame");
    }
  }
}

esp_err_t VoiceController::enqueue_outbound_text(const char* data, size_t size) {
  return enqueue_outbound(OutboundFrameKind::kText, data, size,
                          pdMS_TO_TICKS(100));
}

esp_err_t VoiceController::enqueue_outbound_binary(const uint8_t* data,
                                                    size_t size) {
  return enqueue_outbound(OutboundFrameKind::kBinary, data, size, 0);
}

esp_err_t VoiceController::enqueue_outbound(OutboundFrameKind kind,
                                             const void* data, size_t size,
                                             TickType_t wait_ticks) {
  if (outbound_queue_ == nullptr || data == nullptr || size == 0 ||
      size > OutboundFrame{}.data.size()) {
    return ESP_ERR_INVALID_ARG;
  }
  OutboundFrame frame{};
  frame.kind = kind;
  frame.size = static_cast<uint16_t>(size);
  frame.connection_epoch = outbound_connection_epoch_.load();
  std::memcpy(frame.data.data(), data, size);
  return xQueueSend(outbound_queue_, &frame, wait_ticks) == pdTRUE ? ESP_OK
                                                                     : ESP_ERR_TIMEOUT;
}

void VoiceController::discard_outbound_frames() {
  if (outbound_queue_ != nullptr) xQueueReset(outbound_queue_);
}

void VoiceController::play_pending_audio() {
  if (!tts_active_ || downlink_queue_ == nullptr) return;
  DownlinkPacket packet{};
  for (UBaseType_t inspected = 0; inspected < 16; ++inspected) {
    if (xQueueReceive(downlink_queue_, &packet, 0) != pdTRUE) return;
    // A new tts.start can be queued behind packets from an interrupted
    // generation. Discard stale packets without resetting new-generation
    // frames that arrived immediately after tts.start on the WSS task.
    if (packet.generation_id != active_generation_) continue;
    if (packet.sequence != expected_downlink_sequence_) {
      fail_tts_playback("downlink sequence mismatch");
      return;
    }
    ++expected_downlink_sequence_;

    std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
    size_t samples = 0;
    if (codec_.decode(packet.data.data(), packet.size, pcm.data(), pcm.size(),
                      &samples) != ESP_OK ||
        samples != pcm.size() ||
        audio_->write_speaker_frame(pcm.data(), pcm.size(), 100) != ESP_OK) {
      fail_tts_playback("downlink decode or playback failure");
    }
    return;
  }
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
      .sequence = control_sequence_,
      .timestamp_ms = now_ms(),
      .payload_json = payload_json,
  };
  size_t message_size = 0;
  const auto result = sesame::protocol::serialize_control_event(
      event, message.data(), message.size(), &message_size);
  if (result != sesame::protocol::ControlEventError::kOk) {
    return ESP_ERR_INVALID_ARG;
  }
  const esp_err_t enqueue_result =
      enqueue_outbound_text(message.data(), message_size);
  if (enqueue_result == ESP_OK) ++control_sequence_;
  return enqueue_result;
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
  gateway_connection_.transport_connected();
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
  gateway_connection_.disconnected();
  outbound_connection_epoch_.fetch_add(1);
  discard_outbound_frames();
  schedule_gateway_retry(now_ms());
  session_ready_ = false;
  button_.reset();
  capture_session_.reset();
  if (pcm_preroll_ != nullptr) pcm_preroll_->clear();
  downlink_fault_requested_ = false;
  first_uplink_pending_ = false;
  stop_wake_ack();
  wake_turn_detector_.reset();
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
  enqueue_downlink_packet(data, size);
}

void VoiceController::enqueue_downlink_packet(const uint8_t* data,
                                               size_t size) {
  if (!running_.load() || downlink_queue_ == nullptr) return;
  sesame::protocol::AudioFrame frame{};
  if (sesame::protocol::unpack_audio_frame(data, size, &frame) !=
          sesame::protocol::AudioFrameError::kOk ||
      frame.direction != sesame::protocol::AudioDirection::kDownlink ||
      frame.flags != 0 || frame.stream_id != 2 || frame.generation_id == 0 ||
      frame.payload_size > sesame::audio::OpusCodec::kMaxPacketBytes) {
    return;
  }
  DownlinkPacket& packet = downlink_work_packet_;
  packet.generation_id = frame.generation_id;
  packet.sequence = frame.sequence;
  packet.size = static_cast<uint16_t>(frame.payload_size);
  std::memcpy(packet.data.data(), frame.payload, frame.payload_size);
  if (xQueueSend(downlink_queue_, &packet, 0) != pdTRUE) {
    // The WebSocket task must never mutate playback/turn state. Ask the voice
    // task to fail the generation closed on its next iteration.
    downlink_fault_requested_ = true;
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
    gateway_connection_.session_ready();
    // Update the live configuration immediately, but persist on a dedicated
    // internal-RAM task. This voice task uses a PSRAM stack for Opus and must
    // never invoke NVS, which temporarily disables the external-memory cache.
    copy_identifier(&config_.conversation_id, conversation);
    if (conversation_store_.enqueue(conversation) != ESP_OK) {
      ESP_LOGW(kTag, "could not queue validated conversation identifier");
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
      } else if (std::strcmp(kind, "wakeword_settings") == 0) {
        int threshold_hundredths = 0;
        accepted = integer_field_in_range(payload, "wake_threshold_hundredths",
                                          5, 95, &threshold_hundredths) &&
                   wake_threshold_store_.enqueue(
                       static_cast<uint8_t>(threshold_hundredths)) == ESP_OK &&
                   wake_vad_.set_detection_threshold_hundredths(
                       static_cast<uint8_t>(threshold_hundredths));
      } else if (std::strcmp(kind, "stop") == 0) {
        robot_->emergency_stop();
        accepted = true;
      }
    }
    send_operator_result(accepted, request_id);
    ESP_LOGI(kTag, "operator.control handled: kind=%s accepted=%s",
             kind == nullptr ? "invalid" : kind, accepted ? "true" : "false");
  } else if (std::strcmp(type, "turn.complete") == 0) {
    const char* outcome = string_field(payload, "outcome");
    const char* reason = string_field(payload, "reason");
    const bool valid_reason = reason != nullptr &&
        (std::strcmp(reason, "asr_no_speech") == 0 ||
         std::strcmp(reason, "blank") == 0 ||
         std::strcmp(reason, "filler_only") == 0);
    if (!matches_active_turn(inbound_turn) ||
        turn_state_.state() != sesame::protocol::TurnState::kThinking ||
        outcome == nullptr || std::strcmp(outcome, "discard") != 0 ||
        !valid_reason) {
      ESP_LOGW(kTag, "discarded invalid turn.complete outside its active turn");
      cJSON_Delete(root);
      return;
    }
    planned_generation_ = 0;
    active_generation_ = 0;
    turn_state_.apply(sesame::protocol::TurnEvent::kDiscarded);
    turn_id_.fill('\0');
    if (pcm_preroll_ != nullptr) pcm_preroll_->clear();
    wake_turn_detector_.reset();
    ESP_LOGI(kTag, "P2 turn discarded silently: reason=%s", reason);
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
  // Preserve packets already delivered immediately after tts.start. The
  // playback consumer filters any stale generation itself.
  flush_tts(false);
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
  // Keep the state machine's generation for monotonic validation, but clear
  // the active playback binding. Otherwise BOOT pressed while the next turn
  // is still thinking is incorrectly bound to the previous TTS generation.
  active_generation_ = 0;
  expected_downlink_sequence_ = 0;
  if (!interrupt_pending_) {
    turn_id_.fill('\0');
    if (pcm_preroll_ != nullptr) pcm_preroll_->clear();
    if (wake_turn_detector_.start_followup_wait(now_ms())) {
      ESP_LOGI(kTag,
               "P2 playback completed: generation=%lu; waiting 3000 ms for follow-up speech",
               static_cast<unsigned long>(generation_id));
    } else {
      wake_turn_detector_.reset();
      ESP_LOGW(kTag,
               "P2 playback completed outside TTS state; waiting for wake word");
    }
  } else {
    ESP_LOGI(kTag, "P2 playback completed: generation=%lu",
             static_cast<unsigned long>(generation_id));
  }
}

void VoiceController::fail_tts_playback(const char* reason) {
  flush_tts();
  turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
  turn_id_.fill('\0');
  wake_turn_detector_.reset();
  if (robot_ != nullptr) robot_->emergency_stop();
  ESP_LOGW(kTag, "P2 playback failed closed: %s",
           reason == nullptr ? "unknown" : reason);
}

void VoiceController::flush_tts(bool clear_downlink) {
  if (clear_downlink && downlink_queue_ != nullptr) {
    xQueueReset(downlink_queue_);
  }
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
