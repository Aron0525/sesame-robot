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

namespace sesame::voice {
namespace {

constexpr char kTag[] = "sesame_voice";
constexpr gpio_num_t kVoiceButton = GPIO_NUM_0;

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
  esp_err_t result = codec_.initialize();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "initialize Opus codec: %s", esp_err_to_name(result));
    return result;
  }
  result = wake_vad_.start();
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "initialize custom wake word and VAD pipeline: %s",
             esp_err_to_name(result));
    codec_.shutdown();
    return result;
  }
  result = load_device_config(&config_);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "load device configuration from NVS: %s",
             esp_err_to_name(result));
    stop();
    return result;
  }

  const gpio_config_t button_config{
      .pin_bit_mask = 1ULL << kVoiceButton,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  result = gpio_config(&button_config);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "configure BOOT voice button: %s", esp_err_to_name(result));
    stop();
    return result;
  }

  downlink_queue_ = xQueueCreate(8, sizeof(DownlinkPacket));
  if (downlink_queue_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }

  running_ = true;
  if (xTaskCreatePinnedToCore(task_entry, "sesame_voice", 12288, this, 7,
                              &task_, 1) != pdPASS) {
    running_ = false;
    stop();
    return ESP_ERR_NO_MEM;
  }

  result = gateway_.start(config_, this);
  if (result != ESP_OK) {
    stop();
    return result;
  }
  return ESP_OK;
}

void VoiceController::stop() {
  running_ = false;
  if (task_ != nullptr) {
    for (int attempt = 0; attempt < 50 && task_ != nullptr; ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (task_ != nullptr) {
      vTaskDelete(task_);
      task_ = nullptr;
    }
  }
  gateway_.stop();
  wake_vad_.stop();
  if (downlink_queue_ != nullptr) {
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
  }
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
    const uint64_t timestamp = now_ms();
    const bool pressed = gpio_get_level(kVoiceButton) == 0;
    handle_button(button_.update(pressed, timestamp), timestamp);

    if (session_ready_ && !tts_active_ && voice_capture_enabled_) {
      capture_and_process(timestamp);
      process_wake_vad_signals(timestamp);
    } else {
      play_pending_audio();
      complete_tts_if_drained();
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void VoiceController::handle_button(ButtonEvent event, uint64_t timestamp) {
  if (!session_ready_) return;
  if (event == ButtonEvent::kStartRecording) {
    if (turn_state_.state() == sesame::protocol::TurnState::kListening) {
      ESP_LOGI(kTag, "manual stop during an active listen turn");
      turn_detector_.reset();
      finish_listening();
      return;
    }
    turn_detector_.reset();
    handle_turn_event(turn_detector_.start_from_button(timestamp), timestamp);
  } else if (event == ButtonEvent::kStopRecording ||
             event == ButtonEvent::kMaximumDuration) {
    turn_detector_.reset();
    finish_listening();
  }
}

void VoiceController::capture_and_process(uint64_t timestamp) {
  std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
  if (audio_->read_microphone_frame(pcm.data(), pcm.size(), 100) != ESP_OK) {
    return;
  }
  wake_vad_.feed_pcm(pcm.data(), pcm.size());
  if (turn_detector_.listening()) {
    encode_and_send(pcm.data(), pcm.size(), timestamp);
  }
}

void VoiceController::encode_and_send(const int16_t* pcm, size_t samples,
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
    gateway_.send_binary(message.data(), message_size);
  }
}

void VoiceController::process_wake_vad_signals(uint64_t timestamp) {
  WakeVadSignal signal{};
  while (wake_vad_.read_signal(&signal)) {
    handle_turn_event(turn_detector_.update(timestamp, signal.wake_detected,
                                             signal.vad_speech),
                      timestamp);
  }
}

void VoiceController::handle_turn_event(VoiceTurnEvent event,
                                        uint64_t timestamp) {
  switch (event) {
    case VoiceTurnEvent::kWakeDetected:
      ESP_LOGI(kTag, "wake word detected; starting listen");
      begin_listening(timestamp);
      return;
    case VoiceTurnEvent::kListenStarted:
      begin_listening(timestamp);
      return;
    case VoiceTurnEvent::kListenStopped:
      ESP_LOGI(kTag, "VAD endpoint reached; ending listen turn");
      finish_listening();
      return;
    case VoiceTurnEvent::kWakeTimedOut:
      ESP_LOGI(kTag, "wake timed out without speech");
      return;
    case VoiceTurnEvent::kListenTimedOut:
      ESP_LOGI(kTag, "listen turn reached the 10-second safety limit");
      finish_listening();
      return;
    case VoiceTurnEvent::kNone:
      return;
  }
}

void VoiceController::begin_listening(uint64_t timestamp) {
  if (tts_active_) {
    send_control(sesame::protocol::ControlEventType::kInterrupt, "{}");
    flush_tts();
  }
  if (!turn_state_.apply(sesame::protocol::TurnEvent::kListenRequested)) {
    ESP_LOGW(kTag, "ignored duplicate listen request in state %s",
             sesame::protocol::to_string(turn_state_.state()));
    return;
  }
  ++turn_counter_;
  std::snprintf(turn_id_.data(), turn_id_.size(), "turn_%08lx_%lu",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(turn_counter_));
  audio_sequence_ = 0;
  codec_.reset();
  voice_capture_enabled_ = true;
  send_control(sesame::protocol::ControlEventType::kListenStart, "{}");
}

void VoiceController::finish_listening() {
  if (turn_state_.state() != sesame::protocol::TurnState::kListening) return;
  button_.finish_from_endpoint();
  send_control(sesame::protocol::ControlEventType::kListenStop, "{}");
  turn_state_.apply(sesame::protocol::TurnEvent::kEndpointDetected);
  voice_capture_enabled_ = false;
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
    send_control(sesame::protocol::ControlEventType::kSessionHello,
                 payload.data());
  }
}

void VoiceController::on_gateway_connected() {
  session_ready_ = false;
  session_id_.fill('\0');
  turn_id_.fill('\0');
  control_sequence_ = 0;
  expected_control_sequence_ = 0;
  send_session_hello();
}

void VoiceController::on_gateway_disconnected() {
  session_ready_ = false;
  button_.reset();
  flush_tts();
  turn_detector_.reset();
  voice_capture_enabled_ = true;
  turn_state_.apply(sesame::protocol::TurnEvent::kConnectionLost);
}

void VoiceController::on_gateway_text(const char* data, size_t size) {
  process_control_json(data, size);
}

void VoiceController::on_gateway_binary(const uint8_t* data, size_t size) {
  sesame::protocol::AudioFrame frame{};
  if (sesame::protocol::unpack_audio_frame(data, size, &frame) !=
          sesame::protocol::AudioFrameError::kOk ||
      frame.direction != sesame::protocol::AudioDirection::kDownlink ||
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
  }
}

void VoiceController::process_control_json(const char* data, size_t size) {
  if (data == nullptr || size == 0 ||
      size >= sesame::protocol::kMaxControlFrameBytes) {
    return;
  }
  cJSON* root = cJSON_ParseWithLength(data, size);
  if (root == nullptr) return;
  const char* type = string_field(root, "type");
  const cJSON* payload = payload_of(root);
  const cJSON* sequence =
      cJSON_GetObjectItemCaseSensitive(root, "sequence");
  if (type == nullptr || payload == nullptr || !cJSON_IsNumber(sequence) ||
      sequence->valuedouble < 0 ||
      sequence->valuedouble > 4294967295.0 ||
      static_cast<uint32_t>(sequence->valuedouble) !=
          expected_control_sequence_) {
    cJSON_Delete(root);
    return;
  }
  ++expected_control_sequence_;

  if (std::strcmp(type, "session.ready") == 0) {
    const char* session = string_field(root, "session_id");
    const char* gateway_id = string_field(payload, "gateway_id");
    const char* conversation = string_field(payload, "conversation_id");
    const cJSON* protocol_version =
        cJSON_GetObjectItemCaseSensitive(payload, "protocol_version");
    const cJSON* audio =
        cJSON_GetObjectItemCaseSensitive(payload, "audio");
    const bool valid_audio =
        cJSON_IsObject(audio) &&
        string_field(audio, "codec") != nullptr &&
        std::strcmp(string_field(audio, "codec"), "opus") == 0 &&
        uint_field(audio, "sample_rate") == 16000 &&
        uint_field(audio, "channels") == 1 &&
        uint_field(audio, "frame_duration_ms") == 20;
    if (session != nullptr && gateway_id != nullptr &&
        std::strcmp(gateway_id, config_.gateway_id.data()) == 0 &&
        cJSON_IsNumber(protocol_version) &&
        protocol_version->valueint == 1 && valid_audio &&
        strnlen(session, session_id_.size()) < session_id_.size()) {
      std::strncpy(session_id_.data(), session, session_id_.size() - 1);
      session_ready_ = true;
    }
    if (conversation != nullptr) {
      sesame::transport::save_conversation_id(conversation);
    }
  } else if (std::strcmp(type, "tts.start") == 0) {
    begin_tts(uint_field(payload, "generation_id"));
  } else if (std::strcmp(type, "tts.stop") == 0) {
    finish_tts(uint_field(payload, "generation_id"));
  } else if (std::strcmp(type, "tts.flush") == 0) {
    flush_tts();
  } else if (std::strcmp(type, "expression.set") == 0 && robot_ != nullptr) {
    robot_->set_expression(string_field(payload, "expression"),
                           uint_field(payload, "ttl_ms"));
  } else if (std::strcmp(type, "action.execute") == 0 &&
             robot_ != nullptr) {
    const char* request_id = string_field(root, "request_id");
    const char* action = string_field(payload, "action");
    if (request_id != nullptr && action != nullptr) {
      const cJSON* deadline =
          cJSON_GetObjectItemCaseSensitive(payload, "deadline_ms");
      if (cJSON_IsNumber(deadline) && deadline->valuedouble >= 0) {
        const sesame::robot::ActionRequest request{
            request_id, action, uint_field(payload, "duration_ms"),
            static_cast<uint64_t>(deadline->valuedouble),
        };
        const sesame::robot::ActionDecision decision =
            robot_->execute(request, uint64_field(root, "timestamp_ms"));
        const char* status = "rejected";
        const char* error_code = "\"action_rejected\"";
        if (decision == sesame::robot::ActionDecision::kAllowed) {
          status = "completed";
          error_code = "null";
        } else if (decision == sesame::robot::ActionDecision::kExpired) {
          status = "expired";
          error_code = "\"deadline_expired\"";
        } else if (decision ==
                   sesame::robot::ActionDecision::kUnknownAction) {
          error_code = "\"unknown_action\"";
        } else if (decision ==
                   sesame::robot::ActionDecision::kDurationOutOfRange) {
          error_code = "\"duration_out_of_range\"";
        } else if (decision ==
                   sesame::robot::ActionDecision::kUnsafeState) {
          error_code = "\"unsafe_state\"";
        }
        std::array<char, 128> result_payload{};
        const int length = std::snprintf(
            result_payload.data(), result_payload.size(),
            "{\"status\":\"%s\",\"error_code\":%s}", status, error_code);
        if (length > 0 &&
            static_cast<size_t>(length) < result_payload.size()) {
          send_control(sesame::protocol::ControlEventType::kActionResult,
                       result_payload.data(), request_id);
        }
      }
    }
  }
  cJSON_Delete(root);
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
}

void VoiceController::finish_tts(uint32_t generation_id) {
  if (!tts_active_ || generation_id != active_generation_) return;
  tts_stop_requested_ = true;
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
  turn_detector_.reset();
  voice_capture_enabled_ = true;
}

void VoiceController::flush_tts() {
  if (downlink_queue_ != nullptr) xQueueReset(downlink_queue_);
  if (audio_ != nullptr && audio_->initialized()) {
    audio_->set_amplifier_enabled(false);
  }
  tts_active_ = false;
  tts_stop_requested_ = false;
  active_generation_ = 0;
  expected_downlink_sequence_ = 0;
}

}  // namespace sesame::voice
