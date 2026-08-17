// ESP-SR AFE VAD plus XiaoZhi-style MultiNet custom command recognition.

#include "sesame_voice/wake_vad_engine.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <new>

#include "esp_afe_sr_models.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

#include "sesame_voice/multinet_wakeword_config.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "wake_vad";
constexpr int kWakeCommandId = 1;
constexpr size_t kRuntimeBufferSamples = 1024;
constexpr int kVadMinimumSpeechMs = 60;
constexpr int kVadMinimumNoiseMs = 100;

}  // namespace

struct WakeVadEngine::WakeWordRuntime {
  ~WakeWordRuntime() { shutdown(); }

  bool initialize() {
    models = esp_srmodel_init("model");
    if (models == nullptr || models->num <= 0) {
      ESP_LOGE(kTag, "ESP-SR model partition is unavailable");
      return false;
    }

    model_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (model_name == nullptr ||
        std::strcmp(model_name, zhima::kMultinetModelName) != 0) {
      ESP_LOGE(kTag, "required MultiNet model %s is not in model partition",
               zhima::kMultinetModelName);
      return false;
    }

    multinet = esp_mn_handle_from_name(model_name);
    if (multinet == nullptr ||
        (model_data = multinet->create(model_name, zhima::kDetectionDurationMs)) ==
            nullptr) {
      ESP_LOGE(kTag, "initialize MultiNet model %s", model_name);
      return false;
    }
    // ESP-SR MultiNet applies the threshold but does not expose a reliable
    // status return across model versions; Xiaozhi likewise does not check it.
    multinet->set_det_threshold(model_data, zhima::kWakeThreshold);
    const esp_err_t allocate_result = esp_mn_commands_alloc(multinet, model_data);
    if (allocate_result != ESP_OK) {
      ESP_LOGE(kTag, "allocate MultiNet commands: %s", esp_err_to_name(allocate_result));
      return false;
    }
    commands_allocated = true;
    const esp_err_t add_result =
        esp_mn_commands_add(kWakeCommandId, zhima::kWakeWordPinyin);
    if (add_result != ESP_OK) {
      ESP_LOGE(kTag, "register MultiNet command %s: %s", zhima::kWakeWordPinyin,
               esp_err_to_name(add_result));
      return false;
    }
    esp_mn_error_t* command_errors = esp_mn_commands_update();
    if (command_errors != nullptr) {
      ESP_LOGE(kTag, "MultiNet rejected %d command(s)", command_errors->num);
      for (int index = 0; index < command_errors->num; ++index) {
        const esp_mn_phrase_t* phrase = command_errors->phrases[index];
        ESP_LOGE(kTag, "rejected command: %s", phrase == nullptr ? "<unknown>" : phrase->string);
      }
      return false;
    }
    multinet->print_active_speech_commands(model_data);

    afe_config_t* afe_config =
        afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afe_config == nullptr) {
      ESP_LOGE(kTag, "create AFE configuration");
      return false;
    }
    // INMP441 supplies one microphone channel and no playback reference, so
    // AEC cannot be configured honestly. TTS is excluded by the controller.
    afe_config->aec_init = false;
    afe_config->se_init = false;
    afe_config->ns_init = false;
    afe_config->vad_init = true;
    afe_config->vad_mode = VAD_MODE_0;
    afe_config->vad_min_speech_ms = kVadMinimumSpeechMs;
    afe_config->vad_min_noise_ms = kVadMinimumNoiseMs;
    afe_config->wakenet_init = false;
    afe_config->agc_init = false;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    afe_iface = esp_afe_handle_from_config(afe_config);
    if (afe_iface != nullptr) afe_data = afe_iface->create_from_config(afe_config);
    afe_config_free(afe_config);
    if (afe_iface == nullptr || afe_data == nullptr) {
      ESP_LOGE(kTag, "initialize ESP-SR AFE");
      return false;
    }

    afe_feed_samples = afe_iface->get_feed_chunksize(afe_data);
    multinet_samples = multinet->get_samp_chunksize(model_data);
    if (afe_feed_samples <= 0 || multinet_samples <= 0 ||
        static_cast<size_t>(afe_feed_samples) > afe_input.size() ||
        static_cast<size_t>(multinet_samples) > multinet_input.size()) {
      ESP_LOGE(kTag, "unexpected AFE/MultiNet frame sizes: afe=%d mn=%d",
               afe_feed_samples, multinet_samples);
      return false;
    }
    afe_iface->print_pipeline(afe_data);
    ESP_LOGI(kTag,
             "MultiNet ready: model=%s phrase=%s threshold=%.2f duration=%d ms "
             "AFE feed=%d MultiNet feed=%d",
             model_name, zhima::kWakeWordPinyin, zhima::kWakeThreshold,
             zhima::kDetectionDurationMs, afe_feed_samples, multinet_samples);
    return true;
  }

  void shutdown() {
    if (afe_iface != nullptr && afe_data != nullptr) {
      afe_iface->destroy(afe_data);
    }
    afe_data = nullptr;
    afe_iface = nullptr;
    if (commands_allocated) {
      esp_mn_commands_free();
      commands_allocated = false;
    }
    if (multinet != nullptr && model_data != nullptr) {
      multinet->destroy(model_data);
    }
    model_data = nullptr;
    multinet = nullptr;
    model_name = nullptr;
    if (models != nullptr) esp_srmodel_deinit(models);
    models = nullptr;
    afe_buffered = 0;
    multinet_buffered = 0;
  }

  void reset_detection() {
    multinet_buffered = 0;
    if (multinet != nullptr && model_data != nullptr) multinet->clean(model_data);
  }

  void set_detection_threshold(float threshold) {
    if (multinet != nullptr && model_data != nullptr) {
      multinet->set_det_threshold(model_data, threshold);
    }
  }

  bool process_pcm(const int16_t* pcm, size_t samples, bool wake_enabled,
                   WakeVadSignal* signal) {
    if (pcm == nullptr || samples == 0 || signal == nullptr || afe_iface == nullptr ||
        afe_data == nullptr || multinet == nullptr || model_data == nullptr) {
      return false;
    }
    *signal = {};
    bool produced_signal = false;
    size_t offset = 0;
    while (offset < samples) {
      const size_t copied = std::min(
          samples - offset,
          static_cast<size_t>(afe_feed_samples) - afe_buffered);
      std::memcpy(afe_input.data() + afe_buffered, pcm + offset,
                  copied * sizeof(int16_t));
      afe_buffered += copied;
      offset += copied;
      if (afe_buffered != static_cast<size_t>(afe_feed_samples)) continue;

      afe_iface->feed(afe_data, afe_input.data());
      afe_buffered = 0;
      const afe_fetch_result_t* result =
          afe_iface->fetch_with_delay(afe_data, 0);
      if (result == nullptr || result->ret_value == ESP_FAIL ||
          result->data == nullptr || result->data_size <= 0) {
        continue;
      }
      produced_signal = true;
      signal->vad_speech = result->vad_state == VAD_SPEECH;
      if (wake_enabled &&
          append_multinet(result->data,
                          static_cast<size_t>(result->data_size) / sizeof(int16_t))) {
        signal->wake_detected = true;
      }
    }
    return produced_signal;
  }

 private:
  bool append_multinet(const int16_t* pcm, size_t samples) {
    size_t offset = 0;
    while (offset < samples) {
      const size_t copied = std::min(
          samples - offset,
          static_cast<size_t>(multinet_samples) - multinet_buffered);
      std::memcpy(multinet_input.data() + multinet_buffered, pcm + offset,
                  copied * sizeof(int16_t));
      multinet_buffered += copied;
      offset += copied;
      if (multinet_buffered != static_cast<size_t>(multinet_samples)) continue;

      const esp_mn_state_t state =
          multinet->detect(model_data, multinet_input.data());
      multinet_buffered = 0;
      if (state == ESP_MN_STATE_TIMEOUT) {
        multinet->clean(model_data);
        continue;
      }
      if (state != ESP_MN_STATE_DETECTED) continue;

      const esp_mn_results_t* results = multinet->get_results(model_data);
      bool target_detected = false;
      if (results != nullptr) {
        for (int index = 0; index < results->num; ++index) {
          ESP_LOGI(kTag,
                   "MultiNet result: command_id=%d text=%s probability=%.3f",
                   results->command_id[index], results->string,
                   results->prob[index]);
          target_detected = target_detected ||
                            results->command_id[index] == kWakeCommandId;
        }
      }
      multinet->clean(model_data);
      if (target_detected) {
        ESP_LOGI(kTag, "MultiNet wake word detected: %s",
                 zhima::kWakeWordText);
      }
      return target_detected;
    }
    return false;
  }

  srmodel_list_t* models{nullptr};
  char* model_name{nullptr};
  esp_mn_iface_t* multinet{nullptr};
  model_iface_data_t* model_data{nullptr};
  const esp_afe_sr_iface_t* afe_iface{nullptr};
  esp_afe_sr_data_t* afe_data{nullptr};
  std::array<int16_t, kRuntimeBufferSamples> afe_input{};
  std::array<int16_t, kRuntimeBufferSamples> multinet_input{};
  size_t afe_buffered{0};
  size_t multinet_buffered{0};
  int afe_feed_samples{0};
  int multinet_samples{0};
  bool commands_allocated{false};
};

WakeVadEngine::~WakeVadEngine() { stop(); }

esp_err_t WakeVadEngine::start() {
  if (running_) return ESP_OK;

  wakeword_ = new (std::nothrow) WakeWordRuntime;
  if (wakeword_ == nullptr || !wakeword_->initialize()) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  audio_queue_ = xQueueCreate(kAudioQueueDepth, sizeof(WakeAudioFrame));
  signal_queue_ = xQueueCreate(kSignalQueueDepth, sizeof(WakeVadSignal));
  if (audio_queue_ == nullptr || signal_queue_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }

  wake_enabled_ = true;
  wake_reset_requested_ = true;
  dropped_audio_frames_ = 0;
  running_ = true;
  if (xTaskCreatePinnedToCore(processing_task_entry, "wake_vad_work", 12288,
                              this, 5, &processing_task_, 1) != pdPASS) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(kTag, "ESP-SR AFE VAD + MultiNet wake detector ready");
  return ESP_OK;
}

void WakeVadEngine::stop() {
  running_ = false;
  if (processing_task_ != nullptr) {
    for (int attempt = 0; attempt < 50 && processing_task_ != nullptr;
         ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (processing_task_ != nullptr) {
      vTaskDelete(processing_task_);
      processing_task_ = nullptr;
    }
  }
  if (signal_queue_ != nullptr) {
    vQueueDelete(signal_queue_);
    signal_queue_ = nullptr;
  }
  if (audio_queue_ != nullptr) {
    vQueueDelete(audio_queue_);
    audio_queue_ = nullptr;
  }
  delete wakeword_;
  wakeword_ = nullptr;
  wake_enabled_ = true;
  wake_reset_requested_ = false;
  dropped_audio_frames_ = 0;
}

void WakeVadEngine::set_wake_enabled(bool enabled) {
  if (wake_enabled_.exchange(enabled) != enabled) {
    wake_reset_requested_ = true;
  }
}

bool WakeVadEngine::set_detection_threshold_hundredths(uint8_t hundredths) {
  if (hundredths < 5 || hundredths > 95) return false;
  wake_threshold_hundredths_ = hundredths;
  threshold_update_requested_ = true;
  return true;
}

esp_err_t WakeVadEngine::feed_pcm(const int16_t* pcm, size_t samples) {
  if (!running_ || pcm == nullptr || wakeword_ == nullptr ||
      audio_queue_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  if (samples != sesame::audio::kSamplesPerFrame) return ESP_ERR_INVALID_SIZE;

  WakeAudioFrame frame{};
  std::memcpy(frame.samples.data(), pcm, samples * sizeof(int16_t));
  if (xQueueSend(audio_queue_, &frame, 0) != pdTRUE) {
    const uint32_t dropped = dropped_audio_frames_.fetch_add(1) + 1;
    if (dropped == 1 || (dropped & (dropped - 1)) == 0) {
      ESP_LOGW(kTag, "AFE/MultiNet worker lagged; dropped %lu frame(s)",
               static_cast<unsigned long>(dropped));
    }
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}

bool WakeVadEngine::read_signal(WakeVadSignal* signal) {
  return signal != nullptr && signal_queue_ != nullptr &&
         xQueueReceive(signal_queue_, signal, 0) == pdTRUE;
}

void WakeVadEngine::processing_task_entry(void* context) {
  auto* self = static_cast<WakeVadEngine*>(context);
  self->processing_loop();
  self->processing_task_ = nullptr;
  vTaskDelete(nullptr);
}

void WakeVadEngine::processing_loop() {
  while (running_) {
    WakeAudioFrame frame{};
    if (xQueueReceive(audio_queue_, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }
    if (!running_) return;
    if (wake_reset_requested_.exchange(false)) wakeword_->reset_detection();
    if (threshold_update_requested_.exchange(false)) {
      wakeword_->set_detection_threshold(
          static_cast<float>(wake_threshold_hundredths_.load()) / 100.0f);
    }

    WakeVadSignal signal{};
    if (!wakeword_->process_pcm(frame.samples.data(), frame.samples.size(),
                                 wake_enabled_.load(), &signal)) {
      continue;
    }
    if (signal.wake_detected) {
      // Stop recognition at the model boundary. The controller re-arms only
      // after it has returned to idle, preventing repeated detections of one
      // utterance or a local TTS reply.
      wake_enabled_ = false;
      wake_reset_requested_ = true;
    }
    if (signal_queue_ != nullptr) xQueueSend(signal_queue_, &signal, 0);
  }
}

}  // namespace sesame::voice
