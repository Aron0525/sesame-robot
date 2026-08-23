// Custom TFLite wake-word model plus ESP-SR VAD. The AFE is retained only for
// VAD/end-of-speech timing; its built-in WakeNet result is deliberately ignored.

#include "sesame_voice/wake_vad_engine.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "model_path.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "sesame_voice/zhima_wakeword_config.h"
#include "sesame_voice/zhima_wakeword_model_data.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "wake_vad";
constexpr char kModelPartition[] = "model";
constexpr size_t kTensorArenaBytes = 96 * 1024;
constexpr size_t kExpectedInputBytes =
    zhima::kFrameCount * zhima::kFeatureBins * sizeof(int8_t);

}  // namespace

struct WakeVadEngine::WakeWordRuntime {
  bool initialize() {
    model = tflite::GetModel(zhima::kModelData);
    if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION) {
      ESP_LOGE(kTag, "TFLite schema mismatch: model=%d runtime=%d",
               model == nullptr ? -1 : model->version(), TFLITE_SCHEMA_VERSION);
      return false;
    }
    if (resolver.AddConv2D() != kTfLiteOk ||
        resolver.AddMaxPool2D() != kTfLiteOk ||
        resolver.AddReshape() != kTfLiteOk ||
        resolver.AddFullyConnected() != kTfLiteOk ||
        resolver.AddSoftmax() != kTfLiteOk) {
      ESP_LOGE(kTag, "register TFLite operators");
      return false;
    }

    interpreter = new (std::nothrow)
        tflite::MicroInterpreter(model, resolver, arena.data(), arena.size());
    if (interpreter == nullptr || interpreter->AllocateTensors() != kTfLiteOk) {
      ESP_LOGE(kTag, "allocate TFLite tensors");
      return false;
    }
    input = interpreter->input(0);
    output = interpreter->output(0);
    if (input == nullptr || output == nullptr || input->type != kTfLiteInt8 ||
        output->type != kTfLiteInt8 || input->bytes != kExpectedInputBytes ||
        output->bytes != 2 * sizeof(int8_t)) {
      ESP_LOGE(kTag, "unexpected TFLite tensor contract");
      return false;
    }

    for (size_t sample = 0; sample < zhima::kFrameSize; ++sample) {
      window[sample] = 0.5f - 0.5f * cosf(
          2.0f * static_cast<float>(M_PI) * static_cast<float>(sample) /
          static_cast<float>(zhima::kFrameSize - 1));
    }
    for (size_t bin = 0; bin < zhima::kFeatureBins; ++bin) {
      coefficients[bin] = 2.0f * cosf(
          2.0f * static_cast<float>(M_PI) * static_cast<float>(zhima::kDftBins[bin]) /
          static_cast<float>(zhima::kFrameSize));
    }
    ESP_LOGI(kTag,
             "custom model ready: %s, %u bytes, threshold=%.2f, arena=%u bytes",
             zhima::kModelName, static_cast<unsigned>(zhima::kModelDataLen),
             zhima::kWakeThreshold, static_cast<unsigned>(arena.size()));
    return true;
  }

  ~WakeWordRuntime() { delete interpreter; }

  bool append_and_detect(const int16_t* pcm, size_t samples) {
    if (pcm == nullptr || samples == 0) return false;
    for (size_t sample = 0; sample < samples; ++sample) {
      ring[write_index] = pcm[sample];
      write_index = (write_index + 1) % ring.size();
      if (filled_samples < ring.size()) ++filled_samples;
    }
    if (filled_samples != ring.size()) return false;

    ++frames_since_inference;
    constexpr size_t kFramesPerInference =
        zhima::kInferenceStrideMs / 20;  // AudioHal provides 20 ms frames.
    static_assert(zhima::kInferenceStrideMs % 20 == 0);
    if (frames_since_inference < kFramesPerInference) return false;
    frames_since_inference = 0;

    const float score = evaluate();
    ESP_LOGD(kTag, "custom wake score=%.3f", score);
    const int64_t now_us = esp_timer_get_time();
    if (score < zhima::kWakeThreshold ||
        now_us - last_wake_us < static_cast<int64_t>(zhima::kWakeCooldownMs) * 1000) {
      return false;
    }
    last_wake_us = now_us;
    ESP_LOGI(kTag, "custom wake word detected (score=%.3f)", score);
    return true;
  }

 private:
  float evaluate() {
    for (size_t frame = 0; frame < zhima::kFrameCount; ++frame) {
      for (size_t bin = 0; bin < zhima::kFeatureBins; ++bin) {
        const float coefficient = coefficients[bin];
        float q1 = 0.0f;
        float q2 = 0.0f;
        for (size_t sample = 0; sample < zhima::kFrameSize; ++sample) {
          const size_t position =
              (write_index + frame * zhima::kFrameSize + sample) % ring.size();
          const float normalized =
              static_cast<float>(ring[position]) / 32768.0f * window[sample];
          const float q0 = coefficient * q1 - q2 + normalized;
          q2 = q1;
          q1 = q0;
        }
        const float power =
            (q1 * q1 + q2 * q2 - coefficient * q1 * q2) /
            static_cast<float>(zhima::kFrameSize * zhima::kFrameSize);
        float feature = (logf(fmaxf(power, 0.0f) + 1e-9f) -
                         zhima::kFeatureMean) /
                        zhima::kFeatureStd;
        feature = fminf(6.0f, fmaxf(-6.0f, feature));
        int value = static_cast<int>(lrintf(feature / zhima::kInputScale)) +
                    zhima::kInputZeroPoint;
        value = value < -128 ? -128 : (value > 127 ? 127 : value);
        input->data.int8[frame * zhima::kFeatureBins + bin] =
            static_cast<int8_t>(value);
      }
    }
    if (interpreter->Invoke() != kTfLiteOk) {
      ESP_LOGW(kTag, "TFLite invocation failed");
      return 0.0f;
    }
    return (static_cast<int>(output->data.int8[1]) - output->params.zero_point) *
           output->params.scale;
  }

  const tflite::Model* model{nullptr};
  tflite::MicroMutableOpResolver<6> resolver;
  alignas(16) std::array<uint8_t, kTensorArenaBytes> arena{};
  std::array<float, zhima::kFrameSize> window{};
  std::array<float, zhima::kFeatureBins> coefficients{};
  std::array<int16_t, zhima::kClipSamples> ring{};
  tflite::MicroInterpreter* interpreter{nullptr};
  TfLiteTensor* input{nullptr};
  TfLiteTensor* output{nullptr};
  size_t write_index{0};
  size_t filled_samples{0};
  size_t frames_since_inference{0};
  int64_t last_wake_us{std::numeric_limits<int64_t>::min() / 2};
};

WakeVadEngine::~WakeVadEngine() { stop(); }

esp_err_t WakeVadEngine::start() {
  if (running_) return ESP_OK;

  wakeword_ = new (std::nothrow) WakeWordRuntime;
  if (wakeword_ == nullptr || !wakeword_->initialize()) {
    stop();
    return ESP_ERR_NO_MEM;
  }

  models_ = esp_srmodel_init(kModelPartition);
  if (models_ == nullptr) {
    ESP_LOGE(kTag, "no ESP-SR VAD models in partition '%s'", kModelPartition);
    stop();
    return ESP_ERR_NOT_FOUND;
  }

  afe_config_t* config =
      afe_config_init("M", models_, AFE_TYPE_SR, AFE_MODE_LOW_COST);
  if (config == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  config->wakenet_init = false;
  config->vad_init = true;
  config->vad_min_speech_ms = 128;
  config->vad_min_noise_ms = 800;
  config->vad_delay_ms = 128;

  afe_handle_ = esp_afe_handle_from_config(config);
  if (afe_handle_ != nullptr) {
    afe_data_ = afe_handle_->create_from_config(config);
  }
  afe_config_free(config);
  if (afe_handle_ == nullptr || afe_data_ == nullptr) {
    stop();
    return ESP_FAIL;
  }

  feed_chunk_samples_ = afe_handle_->get_feed_chunksize(afe_data_);
  if (feed_chunk_samples_ <= 0 ||
      static_cast<size_t>(feed_chunk_samples_) > kMaxFeedSamples) {
    stop();
    return ESP_ERR_INVALID_SIZE;
  }
  signal_queue_ = xQueueCreate(8, sizeof(WakeVadSignal));
  if (signal_queue_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }

  running_ = true;
  if (xTaskCreatePinnedToCore(fetch_task_entry, "wake_vad_fetch", 6144, this,
                              6, &fetch_task_, 0) != pdPASS) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  afe_handle_->print_pipeline(afe_data_);
  ESP_LOGI(kTag, "custom TFLite wake word + VAD ready; feed frame=%d samples",
           feed_chunk_samples_);
  return ESP_OK;
}

void WakeVadEngine::stop() {
  running_ = false;
  if (fetch_task_ != nullptr) {
    for (int attempt = 0; attempt < 50 && fetch_task_ != nullptr; ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (fetch_task_ != nullptr) {
      vTaskDelete(fetch_task_);
      fetch_task_ = nullptr;
    }
  }
  if (signal_queue_ != nullptr) {
    vQueueDelete(signal_queue_);
    signal_queue_ = nullptr;
  }
  if (afe_handle_ != nullptr && afe_data_ != nullptr) {
    afe_handle_->destroy(afe_data_);
  }
  afe_data_ = nullptr;
  afe_handle_ = nullptr;
  if (models_ != nullptr) {
    esp_srmodel_deinit(models_);
    models_ = nullptr;
  }
  delete wakeword_;
  wakeword_ = nullptr;
  buffered_samples_ = 0;
  feed_chunk_samples_ = 0;
}

esp_err_t WakeVadEngine::feed_pcm(const int16_t* pcm, size_t samples) {
  if (!running_ || pcm == nullptr || feed_chunk_samples_ <= 0 ||
      wakeword_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  if (samples > kMaxFeedSamples - buffered_samples_) {
    return ESP_ERR_INVALID_SIZE;
  }

  if (wakeword_->append_and_detect(pcm, samples) && signal_queue_ != nullptr) {
    const WakeVadSignal wake_signal{.wake_detected = true, .vad_speech = false};
    xQueueSend(signal_queue_, &wake_signal, 0);
  }

  std::memcpy(feed_buffer_.data() + buffered_samples_, pcm,
              samples * sizeof(int16_t));
  buffered_samples_ += samples;
  const size_t chunk = static_cast<size_t>(feed_chunk_samples_);
  while (buffered_samples_ >= chunk) {
    if (afe_handle_->feed(afe_data_, feed_buffer_.data()) <= 0) {
      return ESP_FAIL;
    }
    buffered_samples_ -= chunk;
    if (buffered_samples_ > 0) {
      std::memmove(feed_buffer_.data(), feed_buffer_.data() + chunk,
                   buffered_samples_ * sizeof(int16_t));
    }
  }
  return ESP_OK;
}

bool WakeVadEngine::read_signal(WakeVadSignal* signal) {
  return signal != nullptr && signal_queue_ != nullptr &&
         xQueueReceive(signal_queue_, signal, 0) == pdTRUE;
}

void WakeVadEngine::fetch_task_entry(void* context) {
  auto* self = static_cast<WakeVadEngine*>(context);
  self->fetch_loop();
  self->fetch_task_ = nullptr;
  vTaskDelete(nullptr);
}

void WakeVadEngine::fetch_loop() {
  while (running_) {
    afe_fetch_result_t* result = afe_handle_->fetch(afe_data_);
    if (!running_) return;
    if (result == nullptr || result->ret_value == ESP_FAIL) {
      ESP_LOGW(kTag, "AFE VAD fetch failed");
      continue;
    }
    // Do not inspect result->wakeup_state: built-in WakeNet is disabled and
    // the embedded TFLite score is the project's only wake decision.
    const WakeVadSignal signal{
        .wake_detected = false,
        .vad_speech = result->vad_state == VAD_SPEECH,
    };
    if (signal_queue_ != nullptr) {
      xQueueSend(signal_queue_, &signal, 0);
    }
  }
}

}  // namespace sesame::voice
