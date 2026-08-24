#include "sesame_voice/voice_controller.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
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

bool integer_field_in_range(const cJSON* object, const char* key, int minimum,
                            int maximum, int* output) {
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(value) || value->valuedouble < minimum ||
      value->valuedouble > maximum ||
      static_cast<int>(value->valuedouble) != value->valuedouble) {
    return false;
  }
  if (output != nullptr) *output = value->valueint;
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
  uint8_t wake_threshold_hundredths = 86;
  result = sesame::transport::load_wake_threshold_hundredths(
      &wake_threshold_hundredths);
  if (result != ESP_OK ||
      !wake_vad_.set_detection_threshold_hundredths(
          wake_threshold_hundredths)) {
    ESP_LOGW(kTag, "load persisted wake threshold: %s; using 0.86",
             esp_err_to_name(result));
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

  downlink_queue_ = xQueueCreate(kPlaybackQueueCapacity, sizeof(DownlinkPacket));
  if (downlink_queue_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  // One coalescing slot prevents telemetry from competing with audio packets
  // for memory or timing while still preserving the newest buffer state.
  playback_stats_queue_ = xQueueCreate(1, sizeof(PlaybackStats));
  if (playback_stats_queue_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  outbound_queue_ = xQueueCreate(kOutboundQueueDepth, sizeof(OutboundFrame));
  control_send_mutex_ = xSemaphoreCreateMutex();
  if (outbound_queue_ == nullptr || control_send_mutex_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }

  running_ = true;
  if (xTaskCreatePinnedToCore(outbound_task_entry, "sesame_uplink",
                              kOutboundTaskStackBytes, this, 5,
                              &outbound_task_, 0) != pdPASS) {
    running_ = false;
    stop();
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreatePinnedToCore(
          playback_task_entry, "sesame_playback", kPlaybackTaskStackBytes,
          this, 8, &playback_task_, 1) != pdPASS) {
    running_ = false;
    stop();
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreatePinnedToCoreWithCaps(
          task_entry, "sesame_voice", 24576, this, 7, &task_, 1,
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
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
      vTaskDeleteWithCaps(task_);
      task_ = nullptr;
    }
  }
  if (playback_task_ != nullptr) {
    for (int attempt = 0; attempt < 50 && playback_task_ != nullptr;
         ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (playback_task_ != nullptr) {
      vTaskDelete(playback_task_);
      playback_task_ = nullptr;
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
  wake_vad_.stop();
  if (downlink_queue_ != nullptr) {
    vQueueDelete(downlink_queue_);
    downlink_queue_ = nullptr;
  }
  if (playback_stats_queue_ != nullptr) {
    vQueueDelete(playback_stats_queue_);
    playback_stats_queue_ = nullptr;
  }
  if (outbound_queue_ != nullptr) {
    vQueueDelete(outbound_queue_);
    outbound_queue_ = nullptr;
  }
  if (control_send_mutex_ != nullptr) {
    vSemaphoreDelete(control_send_mutex_);
    control_send_mutex_ = nullptr;
  }
  codec_.shutdown();
}

void VoiceController::task_entry(void* context) {
  auto* self = static_cast<VoiceController*>(context);
  self->run();
  self->task_ = nullptr;
  vTaskDeleteWithCaps(nullptr);
}

void VoiceController::playback_task_entry(void* context) {
  auto* self = static_cast<VoiceController*>(context);
  self->run_playback();
  self->playback_task_ = nullptr;
  vTaskDelete(nullptr);
}

void VoiceController::outbound_task_entry(void* context) {
  auto* self = static_cast<VoiceController*>(context);
  self->run_outbound();
  self->outbound_task_ = nullptr;
  vTaskDelete(nullptr);
}

void VoiceController::run_outbound() {
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
    const esp_err_t result =
        frame.kind == OutboundFrameKind::kText
            ? gateway_.send_text(reinterpret_cast<const char*>(frame.data.data()),
                                 frame.size)
            : gateway_.send_binary(frame.data.data(), frame.size);
    if (result != ESP_OK) {
      ESP_LOGW(kTag, "outbound WSS send failed: kind=%s result=%s",
               frame.kind == OutboundFrameKind::kText ? "text" : "binary",
               esp_err_to_name(result));
      discard_outbound_frames();
      transport_fault_requested_ = true;
    }
  }
}

void VoiceController::run() {
  while (running_) {
    const uint64_t timestamp = now_ms();
    if (transport_fault_requested_.exchange(false)) {
      ESP_LOGW(kTag, "resetting WSS after an unrecoverable outbound gap");
      session_ready_ = false;
      outbound_connection_epoch_.fetch_add(1);
      discard_outbound_frames();
      if (gateway_.force_reconnect() != ESP_OK) {
        ESP_LOGE(kTag, "failed to restart WSS after outbound fault");
      }
    }
    send_pending_playback_stats();
    const bool pressed = gpio_get_level(kVoiceButton) == 0;
    if (session_ready_) {
      handle_button(button_.update(pressed, timestamp), timestamp);
    } else {
      // Do not let a press during the handshake toggle RecordingButton's
      // internal state without starting a corresponding gateway turn.
      button_.reset();
    }

    if (tts_playback_complete_) {
      complete_tts_if_drained();
    }

    if (session_ready_ && !tts_active_ && !playback_busy_ &&
        voice_capture_enabled_) {
      capture_and_process(timestamp);
      process_wake_vad_signals(timestamp);
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void VoiceController::run_playback() {
  bool playback_started = false;
  uint32_t playback_generation = 0;
  auto& pcm = playback_pcm_;
  auto& silence = playback_silence_;

  while (running_) {
    if (!tts_active_ || downlink_queue_ == nullptr) {
      if (playback_started) {
        playback_busy_ = true;
        if (audio_->stop_speaker() != ESP_OK) {
          ESP_LOGW(kTag, "failed to stop speaker after playback flush");
        }
        playback_busy_ = false;
      }
      playback_started = false;
      playback_started_ = false;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    const uint32_t generation = active_generation_;
    if (generation == 0) {
      if (playback_started) {
        playback_busy_ = true;
        audio_->stop_speaker();
        playback_busy_ = false;
      }
      playback_started = false;
      playback_started_ = false;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (playback_started && playback_generation != generation) {
      playback_busy_ = true;
      audio_->stop_speaker();
      playback_busy_ = false;
      playback_started = false;
      playback_started_ = false;
      continue;
    }
    if (playback_control_.paused()) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    if (!playback_started) {
      const size_t queued = uxQueueMessagesWaiting(downlink_queue_);
      if (!can_start_playback(queued, tts_stop_requested_)) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      playback_generation = generation;
      const size_t preload_frames =
          queued < sesame::audio::kSpeakerDmaDescriptorFrames
              ? queued
              : sesame::audio::kSpeakerDmaDescriptorFrames;
      // Keep codec reset/interrupt handling out of the whole DMA bootstrap.
      // Releasing this flag between descriptors can let a new generation
      // reset the shared Opus decoder before the speaker has been enabled.
      playback_busy_ = true;
      bool preload_ok = preload_frames > 0;
      for (size_t index = 0; index < preload_frames && preload_ok; ++index) {
        DownlinkPacket& packet = playback_work_packet_;
        packet = {};
        if (xQueueReceive(downlink_queue_, &packet, 0) != pdTRUE) {
          preload_ok = false;
          break;
        }
        if (!tts_active_ ||
            packet.generation_id != playback_generation ||
            packet.generation_id != active_generation_) {
          preload_ok = false;
          break;
        }
        playback_telemetry_.note_buffered(
            uxQueueMessagesWaiting(downlink_queue_));
        size_t samples = 0;
        const int64_t decode_started_us = esp_timer_get_time();
        const bool decoded =
            codec_.decode(packet.data.data(), packet.size, pcm.data(),
                          pcm.size(), &samples) == ESP_OK &&
            samples == pcm.size();
        playback_telemetry_.note_decode_duration(static_cast<uint32_t>(
            esp_timer_get_time() - decode_started_us));
        const int64_t i2s_started_us = esp_timer_get_time();
        const bool preloaded =
            decoded &&
            audio_->preload_speaker_frame(pcm.data(), pcm.size()) == ESP_OK;
        if (decoded) {
          playback_telemetry_.note_i2s_duration(static_cast<uint32_t>(
              esp_timer_get_time() - i2s_started_us));
        }
        if (!preloaded) {
          playback_telemetry_.note_dropped_packet();
          ESP_LOGE(kTag,
                   "failed to preload TTS packet: generation=%lu sequence=%lu",
                   static_cast<unsigned long>(packet.generation_id),
                   static_cast<unsigned long>(packet.sequence));
          preload_ok = false;
          break;
        }
        playback_telemetry_.note_rendered();
      }
      if (!preload_ok || audio_->start_speaker() != ESP_OK) {
        if (preload_ok) playback_telemetry_.note_dropped_packet();
        if (audio_->discard_speaker_preload() != ESP_OK) {
          ESP_LOGE(kTag, "failed to reset speaker after DMA preload failure");
          transport_fault_requested_ = true;
        }
        queue_playback_stats(false, true);
        tts_active_ = false;
        tts_playback_complete_ = true;
        playback_busy_ = false;
        playback_started = false;
        playback_started_ = false;
        ESP_LOGE(kTag, "failed to start preloaded TTS playback");
        continue;
      }
      playback_started = true;
      playback_started_ = true;
      playback_busy_ = false;
      ESP_LOGI(kTag, "start TTS playback: generation=%lu queued=%u",
               static_cast<unsigned long>(generation),
               static_cast<unsigned>(queued));
      queue_playback_stats(true);
    }

    DownlinkPacket& packet = playback_work_packet_;
    packet = {};
    if (xQueueReceive(downlink_queue_, &packet, pdMS_TO_TICKS(20)) != pdTRUE) {
      if (!tts_active_ || generation != active_generation_) {
        playback_started = false;
        playback_started_ = false;
        continue;
      }
      if (tts_stop_requested_ && uxQueueMessagesWaiting(downlink_queue_) == 0) {
        // Eight 20-ms DMA descriptors can still contain real audio after the
        // software queue drains. Wait for that tail before declaring audible
        // completion, matching the proven standalone MAX98357 path.
        playback_busy_ = true;
        vTaskDelay(pdMS_TO_TICKS(160));
        if (!tts_active_ || generation != active_generation_) {
          audio_->stop_speaker();
          playback_busy_ = false;
          playback_started = false;
          playback_started_ = false;
          continue;
        }
        const bool stopped = audio_->stop_speaker() == ESP_OK;
        if (!stopped) playback_telemetry_.note_dropped_packet();
        const bool playback_succeeded =
            stopped && playback_telemetry_.healthy() &&
            playback_telemetry_.rendered_frames() ==
                expected_downlink_sequence_.load();
        // playback_complete is a terminal acknowledgement. Success is
        // independently determined by the rendered/error counters so failed
        // playback is reported immediately instead of becoming a timeout.
        queue_playback_stats(false, true);
        if (!playback_succeeded) {
          ESP_LOGW(kTag, "TTS playback completed with render errors");
        }
        tts_active_ = false;
        tts_playback_complete_ = true;
        playback_busy_ = false;
        playback_started = false;
        playback_started_ = false;
      } else {
        // Preserve the 20-ms output cadence rather than replaying stale DMA
        // data when a network burst is late.
        audio_->write_speaker_frame(silence.data(), silence.size(), 100);
        playback_telemetry_.note_underflow();
        queue_playback_stats(true);
      }
      continue;
    }
    // Claim the codec/speaker critical section before validating generation.
    // A concurrent flush can now either invalidate this packet first, or wait
    // for playback_busy_ before resetting the shared Opus codec.
    playback_busy_ = true;
    if (packet.generation_id != playback_generation ||
        packet.generation_id != active_generation_) {
      playback_busy_ = false;
      continue;
    }
    playback_telemetry_.note_buffered(
        uxQueueMessagesWaiting(downlink_queue_));

    size_t samples = 0;
    const int64_t decode_started_us = esp_timer_get_time();
    const bool decoded =
        codec_.decode(packet.data.data(), packet.size, pcm.data(), pcm.size(),
                      &samples) == ESP_OK &&
        samples == pcm.size();
    playback_telemetry_.note_decode_duration(static_cast<uint32_t>(
        esp_timer_get_time() - decode_started_us));
    const int64_t i2s_started_us = esp_timer_get_time();
    const bool written =
        decoded && audio_->write_speaker_frame(pcm.data(), pcm.size(), 100) == ESP_OK;
    if (decoded) {
      playback_telemetry_.note_i2s_duration(static_cast<uint32_t>(
          esp_timer_get_time() - i2s_started_us));
    }
    playback_busy_ = false;
    if (!written) {
      playback_telemetry_.note_dropped_packet();
      ESP_LOGW(kTag, "discarding failed TTS packet: generation=%lu sequence=%lu",
               static_cast<unsigned long>(packet.generation_id),
               static_cast<unsigned long>(packet.sequence));
    } else {
      const uint32_t rendered_frames = playback_telemetry_.note_rendered();
      if (should_report_playback_stats(rendered_frames, false)) {
        queue_playback_stats(true);
      }
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
      .sequence = audio_sequence_.current(),
      .timestamp_ms = timestamp,
      .payload = opus.data(),
      .payload_size = opus_size,
  };
  size_t message_size = 0;
  if (sesame::protocol::pack_audio_frame(
          frame, message.data(), message.size(), &message_size) ==
      sesame::protocol::AudioFrameError::kOk) {
    if (enqueue_outbound_binary(message.data(), message_size) == ESP_OK) {
      audio_sequence_.commit_if(true);
    } else {
      ESP_LOGW(kTag, "uplink queue full; resetting protocol session");
      transport_fault_requested_ = true;
    }
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
      begin_listening(timestamp, CaptureTrigger::kWakeword);
      return;
    case VoiceTurnEvent::kListenStarted:
      begin_listening(timestamp, CaptureTrigger::kManual);
      return;
    case VoiceTurnEvent::kFollowUpListenStarted:
      begin_listening(timestamp, CaptureTrigger::kFollowup);
      return;
    case VoiceTurnEvent::kListenStopped:
      ESP_LOGI(kTag, "VAD endpoint reached; ending listen turn");
      finish_listening();
      return;
    case VoiceTurnEvent::kWakeTimedOut:
      ESP_LOGI(kTag, "wake timed out without speech");
      cancel_listening("wake_timeout");
      return;
    case VoiceTurnEvent::kFollowUpTimedOut:
      ESP_LOGI(kTag, "follow-up window timed out without speech");
      cancel_listening("follow_up_timeout");
      return;
    case VoiceTurnEvent::kListenTimedOut:
      ESP_LOGI(kTag, "listen turn reached the 10-second safety limit");
      finish_listening();
      return;
    case VoiceTurnEvent::kNone:
      return;
  }
}

void VoiceController::begin_listening(uint64_t timestamp,
                                      CaptureTrigger capture_trigger) {
  if (tts_active_) {
    send_control(sesame::protocol::ControlEventType::kInterrupt, "{}");
    flush_tts();
    for (int attempt = 0; playback_busy_ && attempt < 40; ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (playback_busy_) {
      ESP_LOGE(kTag, "playback did not quiesce before microphone capture");
      transport_fault_requested_ = true;
      return;
    }
  }
  if (audio_->reset_microphone_capture() != ESP_OK) {
    ESP_LOGE(kTag, "failed to reset microphone DMA before listen turn");
    transport_fault_requested_ = true;
    return;
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
  audio_sequence_.reset();
  if (codec_.reset() != ESP_OK) {
    turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
    transport_fault_requested_ = true;
    return;
  }
  voice_capture_enabled_ = true;
  std::array<char, 48> payload{};
  const int length = std::snprintf(payload.data(), payload.size(),
                                   "{\"trigger\":\"%s\"}",
                                   capture_trigger_name(capture_trigger));
  if (length > 0 && static_cast<size_t>(length) < payload.size()) {
    send_control(sesame::protocol::ControlEventType::kListenStart,
                 payload.data());
  }
}

void VoiceController::finish_listening() {
  if (turn_state_.state() != sesame::protocol::TurnState::kListening) return;
  button_.finish_from_endpoint();
  send_control(sesame::protocol::ControlEventType::kListenStop, "{}");
  turn_state_.apply(sesame::protocol::TurnEvent::kEndpointDetected);
  voice_capture_enabled_ = false;
}

void VoiceController::cancel_listening(const char* reason) {
  if (turn_state_.state() != sesame::protocol::TurnState::kListening ||
      reason == nullptr) {
    return;
  }
  ESP_LOGI(kTag, "cancel listen locally: reason=%s", reason);
  send_control(sesame::protocol::ControlEventType::kListenStop, "{}");
  turn_state_.apply(sesame::protocol::TurnEvent::kInterrupted);
  voice_capture_enabled_ = true;
}

esp_err_t VoiceController::send_control(
    sesame::protocol::ControlEventType type, const char* payload_json,
    const char* request_id, bool include_active_turn,
    const char* turn_id_override) {
  if (control_send_mutex_ == nullptr) {
    transport_fault_requested_ = true;
    return ESP_ERR_INVALID_STATE;
  }
  if (xSemaphoreTake(control_send_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
    transport_fault_requested_ = true;
    return ESP_ERR_TIMEOUT;
  }
  const bool is_session_hello =
      type == sesame::protocol::ControlEventType::kSessionHello;
  if (!is_session_hello && !session_ready_) {
    xSemaphoreGive(control_send_mutex_);
    return ESP_ERR_INVALID_STATE;
  }
  const esp_err_t result = send_control_locked(
      type, payload_json, request_id, include_active_turn, turn_id_override);
  xSemaphoreGive(control_send_mutex_);
  if (result != ESP_OK) transport_fault_requested_ = true;
  return result;
}

esp_err_t VoiceController::send_control_locked(
    sesame::protocol::ControlEventType type, const char* payload_json,
    const char* request_id, bool include_active_turn,
    const char* turn_id_override) {
  std::array<char, 1024> message{};
  const sesame::protocol::ControlEvent event{
      .type = type,
      .session_id = session_id_[0] == '\0' ? nullptr : session_id_.data(),
      .turn_id = turn_id_override != nullptr
                     ? turn_id_override
                     : (include_active_turn && turn_id_[0] != '\0'
                            ? turn_id_.data()
                            : nullptr),
      .request_id = request_id,
      .sequence = control_sequence_.current(),
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
  control_sequence_.commit_if(enqueue_result == ESP_OK);
  return enqueue_result;
}

esp_err_t VoiceController::enqueue_outbound_text(const char* data,
                                                 size_t size) {
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
  return xQueueSend(outbound_queue_, &frame, wait_ticks) == pdTRUE
             ? ESP_OK
             : ESP_ERR_TIMEOUT;
}

void VoiceController::discard_outbound_frames() {
  if (outbound_queue_ != nullptr) xQueueReset(outbound_queue_);
}

void VoiceController::queue_playback_stats(bool playback_started,
                                           bool playback_complete) {
  if (playback_stats_queue_ == nullptr || downlink_queue_ == nullptr) return;
  const uint32_t generation_id = active_generation_;
  if (generation_id == 0) return;

  const PlaybackStats stats = normalize_playback_startup_ack(
      playback_telemetry_.snapshot(uxQueueMessagesWaiting(downlink_queue_),
                                   playback_started, playback_control_.paused(),
                                   playback_complete));
  // A newer tts.start can race a queued report from the old generation. Never
  // let such a report influence the gateway's current flow-control window.
  if (stats.generation_id != generation_id) return;
  xQueueOverwrite(playback_stats_queue_, &stats);
}

void VoiceController::send_pending_playback_stats() {
  if (playback_stats_queue_ == nullptr) return;
  PlaybackStats stats{};
  if (xQueueReceive(playback_stats_queue_, &stats, 0) != pdTRUE ||
      stats.generation_id == 0 || !session_ready_ ||
      (!tts_active_ && !stats.playback_complete) ||
      stats.generation_id != active_generation_) {
    return;
  }

  std::array<char, 1024> payload{};
  const int length = std::snprintf(
      payload.data(), payload.size(),
      "{\"generation_id\":%lu,\"buffered_packets\":%u,"
      "\"buffered_ms\":%u,\"startup_packets\":%u,"
      "\"low_watermark_packets\":%u,\"high_watermark_packets\":%u,"
      "\"min_buffered_packets\":%u,\"max_buffered_packets\":%u,"
      "\"underflow_count\":%lu,\"dropped_packet_count\":%lu,"
      "\"stale_generation_count\":%lu,\"out_of_order_count\":%lu,"
      "\"sequence_discontinuity_count\":%lu,\"playback_started\":%s,"
      "\"paused\":%s,\"decode_last_us\":%lu,\"decode_max_us\":%lu,"
      "\"decode_avg_us\":%lu,\"i2s_last_us\":%lu,\"i2s_max_us\":%lu,"
      "\"i2s_avg_us\":%lu,\"rendered_frames\":%lu,"
      "\"playback_complete\":%s}",
      static_cast<unsigned long>(stats.generation_id),
      static_cast<unsigned>(stats.buffered_packets),
      static_cast<unsigned>(stats.buffered_ms),
      static_cast<unsigned>(stats.startup_packets),
      static_cast<unsigned>(stats.low_watermark_packets),
      static_cast<unsigned>(stats.high_watermark_packets),
      static_cast<unsigned>(stats.min_buffered_packets),
      static_cast<unsigned>(stats.max_buffered_packets),
      static_cast<unsigned long>(stats.underflow_count),
      static_cast<unsigned long>(stats.dropped_packet_count),
      static_cast<unsigned long>(stats.stale_generation_count),
      static_cast<unsigned long>(stats.out_of_order_count),
      static_cast<unsigned long>(stats.sequence_discontinuity_count),
      stats.playback_started ? "true" : "false",
      stats.paused ? "true" : "false",
      static_cast<unsigned long>(stats.decode_last_us),
      static_cast<unsigned long>(stats.decode_max_us),
      static_cast<unsigned long>(stats.decode_avg_us),
      static_cast<unsigned long>(stats.i2s_last_us),
      static_cast<unsigned long>(stats.i2s_max_us),
      static_cast<unsigned long>(stats.i2s_avg_us),
      static_cast<unsigned long>(stats.rendered_frames),
      stats.playback_complete ? "true" : "false");
  if (length <= 0 || static_cast<size_t>(length) >= payload.size()) return;
  send_control(sesame::protocol::ControlEventType::kPlaybackStats,
               payload.data(), nullptr, true, playback_turn_.turn_id());
}

void VoiceController::send_operator_result(bool accepted,
                                           const char* request_id) {
  if (request_id == nullptr) return;
  const char* payload = accepted
                            ? "{\"status\":\"completed\",\"error_code\":null}"
                            : "{\"status\":\"rejected\",\"error_code\":\"control_rejected\"}";
  send_control(sesame::protocol::ControlEventType::kActionResult, payload,
               request_id, false);
}

esp_err_t VoiceController::send_session_hello_locked() {
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
    return send_control_locked(sesame::protocol::ControlEventType::kSessionHello,
                               payload.data(), nullptr, true, nullptr);
  }
  return ESP_ERR_INVALID_SIZE;
}

void VoiceController::on_gateway_connected() {
  session_ready_ = false;
  if (control_send_mutex_ == nullptr ||
      xSemaphoreTake(control_send_mutex_, portMAX_DELAY) != pdTRUE) {
    transport_fault_requested_ = true;
    return;
  }
  outbound_connection_epoch_.fetch_add(1);
  discard_outbound_frames();
  session_id_.fill('\0');
  turn_id_.fill('\0');
  control_sequence_.reset();
  expected_control_sequence_ = 0;
  const esp_err_t hello_result = send_session_hello_locked();
  xSemaphoreGive(control_send_mutex_);
  if (hello_result != ESP_OK) transport_fault_requested_ = true;
}

void VoiceController::on_gateway_disconnected() {
  session_ready_ = false;
  if (control_send_mutex_ != nullptr &&
      xSemaphoreTake(control_send_mutex_, portMAX_DELAY) == pdTRUE) {
    outbound_connection_epoch_.fetch_add(1);
    discard_outbound_frames();
    xSemaphoreGive(control_send_mutex_);
  } else {
    transport_fault_requested_ = true;
  }
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
      frame.flags != 0 || frame.stream_id != 2 ||
      frame.payload_size > sesame::audio::OpusCodec::kMaxPacketBytes ||
      !tts_active_) {
    return;
  }
  if (frame.generation_id != active_generation_) {
    playback_telemetry_.note_stale_generation();
    queue_playback_stats(playback_started_);
    return;
  }
  if (frame.sequence != expected_downlink_sequence_.load()) {
    playback_telemetry_.note_out_of_order();
    playback_telemetry_.note_sequence_discontinuity();
    queue_playback_stats(playback_started_);
    send_pending_playback_stats();
    flush_tts();
    return;
  }
  expected_downlink_sequence_.fetch_add(1);
  DownlinkPacket packet{
      .generation_id = frame.generation_id,
      .sequence = frame.sequence,
      .size = static_cast<uint16_t>(frame.payload_size),
      .data = {},
  };
  std::memcpy(packet.data.data(), frame.payload, frame.payload_size);
  if (downlink_queue_ == nullptr ||
      xQueueSend(downlink_queue_, &packet, 0) != pdTRUE) {
    playback_telemetry_.note_dropped_packet();
    queue_playback_stats(playback_started_);
    send_pending_playback_stats();
    flush_tts();
    return;
  }
  const size_t queued = uxQueueMessagesWaiting(downlink_queue_);
  playback_telemetry_.note_buffered(queued);
  if (should_report_playback_stats(frame.sequence + 1, false)) {
    queue_playback_stats(playback_started_);
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
  } else if (std::strcmp(type, "turn.complete") == 0) {
    // Gateway sends this terminal event for a valid listen turn that produced
    // no usable speech, so there will be no tts.start/tts.stop pair to bring
    // the local turn state out of Thinking.  Accept only the active turn's
    // explicit discard; a delayed completion from an older turn must not
    // cancel a newer recording.
    const char* incoming_turn_id = string_field(root, "turn_id");
    const char* outcome = string_field(payload, "outcome");
    if (incoming_turn_id != nullptr && outcome != nullptr &&
        std::strcmp(outcome, "discard") == 0 && turn_id_[0] != '\0' &&
        std::strcmp(incoming_turn_id, turn_id_.data()) == 0 &&
        turn_state_.state() == sesame::protocol::TurnState::kThinking) {
      turn_state_.apply(sesame::protocol::TurnEvent::kInterrupted);
      turn_detector_.reset();
      button_.finish_from_endpoint();
      voice_capture_enabled_ = true;
      ESP_LOGI(kTag, "discarded turn completed; ready for the next listen");
    }
  } else if (std::strcmp(type, "tts.start") == 0) {
    const char* incoming_turn_id = string_field(root, "turn_id");
    // Gateway-local PCM diagnostics use their own authenticated `test_`
    // turn and deliberately have no preceding microphone/listen transition.
    const bool is_gateway_local_test =
        incoming_turn_id != nullptr &&
        std::strncmp(incoming_turn_id, "test_", 5) == 0;
    begin_tts(uint_field(payload, "generation_id"), incoming_turn_id,
              is_gateway_local_test);
  } else if (std::strcmp(type, "tts.stop") == 0) {
    finish_tts(uint_field(payload, "generation_id"));
  } else if (std::strcmp(type, "tts.pause") == 0) {
    if (playback_control_.set_paused(uint_field(payload, "generation_id"),
                                     true)) {
      queue_playback_stats(playback_started_);
    }
  } else if (std::strcmp(type, "tts.resume") == 0) {
    if (playback_control_.set_paused(uint_field(payload, "generation_id"),
                                     false)) {
      queue_playback_stats(playback_started_);
    }
  } else if (std::strcmp(type, "tts.flush") == 0) {
    flush_tts();
  } else if (std::strcmp(type, "operator.control") == 0) {
    const char* request_id = string_field(root, "request_id");
    const char* kind = string_field(payload, "kind");
    bool accepted = false;
    if (robot_ != nullptr && request_id != nullptr && kind != nullptr) {
      if (std::strcmp(kind, "action") == 0) {
        accepted = robot_->execute_operator_action(
            string_field(payload, "action"));
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
        accepted = integer_field_in_range(payload, "frame_delay_ms", 10,
                                          1000, &frame_delay_ms) &&
                   integer_field_in_range(payload, "walk_cycles", 1, 50,
                                          &walk_cycles) &&
                   integer_field_in_range(payload, "motor_current_delay_ms",
                                          0, 500, &motor_current_delay_ms) &&
                   robot_->configure_motion(frame_delay_ms, walk_cycles,
                                            motor_current_delay_ms);
      } else if (std::strcmp(kind, "wakeword_settings") == 0) {
        int threshold_hundredths = 0;
        accepted = integer_field_in_range(payload,
                                          "wake_threshold_hundredths", 5, 95,
                                          &threshold_hundredths) &&
                   sesame::transport::save_wake_threshold_hundredths(
                       static_cast<uint8_t>(threshold_hundredths)) == ESP_OK &&
                   wake_vad_.set_detection_threshold_hundredths(
                       static_cast<uint8_t>(threshold_hundredths));
      } else if (std::strcmp(kind, "stop") == 0) {
        robot_->emergency_stop();
        accepted = true;
      }
    }
    send_operator_result(accepted, request_id);
    ESP_LOGI(kTag, "operator control: kind=%s accepted=%s",
             kind == nullptr ? "invalid" : kind, accepted ? "true" : "false");
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

void VoiceController::begin_tts(uint32_t generation_id,
                                const char* incoming_turn_id,
                                bool is_gateway_local_test) {
  if (generation_id == 0 || incoming_turn_id == nullptr) return;
  flush_tts(false);
  for (int attempt = 0; playback_busy_ && attempt < 40; ++attempt) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (playback_busy_) {
    ESP_LOGE(kTag, "cannot reset Opus codec while prior playback is active");
    return;
  }
  const bool accepted = is_gateway_local_test
                            ? turn_state_.start_test_generation(generation_id)
                            : turn_state_.start_generation(generation_id);
  if (!accepted) {
    ESP_LOGW(kTag, "rejecting TTS generation %lu in state %s",
             static_cast<unsigned long>(generation_id),
             sesame::protocol::to_string(turn_state_.state()));
    return;
  }
  if (!playback_turn_.bind(incoming_turn_id, generation_id)) {
    turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
    ESP_LOGW(kTag, "rejecting TTS generation with invalid turn binding");
    return;
  }
  active_generation_ = generation_id;
  playback_control_.begin(generation_id);
  playback_telemetry_.begin_generation(generation_id);
  expected_downlink_sequence_ = 0;
  tts_stop_requested_ = false;
  tts_playback_complete_ = false;
  if (codec_.reset() != ESP_OK) {
    turn_state_.apply(sesame::protocol::TurnEvent::kFailed);
    playback_turn_.clear();
    playback_control_.clear();
    active_generation_ = 0;
    transport_fault_requested_ = true;
    return;
  }
  playback_started_ = false;
  tts_active_ = true;
  // Publish a state transition even before the first audio packet arrives.
  // This makes a rejected/stalled downlink observable at the gateway instead
  // of looking identical to a silent speaker.
  queue_playback_stats(false);
}

void VoiceController::finish_tts(uint32_t generation_id) {
  if (!tts_active_ || generation_id != active_generation_) return;
  tts_stop_requested_ = true;
}

void VoiceController::complete_tts_if_drained() {
  if (!tts_playback_complete_.exchange(false)) {
    return;
  }
  const uint32_t generation_id = active_generation_;
  tts_stop_requested_ = false;
  turn_state_.stop_generation(generation_id);
  active_generation_ = 0;
  playback_control_.clear();
  playback_turn_.clear();
  turn_detector_.reset();
  voice_capture_enabled_ = true;
  const uint64_t timestamp = now_ms();
  handle_turn_event(turn_detector_.start_follow_up(timestamp), timestamp);
}

void VoiceController::flush_tts(bool reset_turn_state) {
  if (downlink_queue_ != nullptr) xQueueReset(downlink_queue_);
  if (playback_stats_queue_ != nullptr) xQueueReset(playback_stats_queue_);
  tts_active_ = false;
  tts_stop_requested_ = false;
  tts_playback_complete_ = false;
  active_generation_ = 0;
  playback_started_ = false;
  expected_downlink_sequence_ = 0;
  playback_control_.clear();
  playback_turn_.clear();
  if (reset_turn_state) {
    turn_state_.apply(sesame::protocol::TurnEvent::kInterrupted);
    turn_detector_.reset();
    voice_capture_enabled_ = true;
  }
}

}  // namespace sesame::voice
