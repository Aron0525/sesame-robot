// Embedded 你好芝麻 TFLite WakeNet plus ESP-SR VAD.

#include "sesame_voice/wake_vad_engine.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vad.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "sesame_voice/zhima_wakeword_config.h"
#include "sesame_voice/zhima_wakeword_model_data.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "wake_vad";
constexpr size_t kTensorArenaBytes = 96 * 1024;
constexpr size_t kExpectedInputBytes =
    zhima::kFrameCount * zhima::kFeatureBins * sizeof(int8_t);
// A wake word is a single utterance. One qualifying rolling window is enough;
// VAD filtering protects the later recording transition from short noise.
constexpr size_t kWakeConfirmationsRequired = 1;
constexpr int kVadMode = VAD_MODE_3;
constexpr int kVadSampleRateHz = 16000;
constexpr int kVadFrameMs = 20;
constexpr int kVadMinimumSpeechMs = 60;
constexpr int kVadMinimumNoiseMs = 40;

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
    if (score < zhima::kWakeThreshold) {
      consecutive_wake_scores = 0;
      return false;
    }
    if (now_us - last_wake_us <
        static_cast<int64_t>(zhima::kWakeCooldownMs) * 1000) {
      consecutive_wake_scores = 0;
      return false;
    }
    if (consecutive_wake_scores < kWakeConfirmationsRequired) {
      ++consecutive_wake_scores;
    }
    if (consecutive_wake_scores < kWakeConfirmationsRequired) {
      ESP_LOGD(kTag, "WakeNet candidate (score=%.3f)", score);
      return false;
    }
    consecutive_wake_scores = 0;
    last_wake_us = now_us;
    ESP_LOGI(kTag, "custom wake word confirmed (score=%.3f)", score);
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
  size_t consecutive_wake_scores{0};
  int64_t last_wake_us{std::numeric_limits<int64_t>::min() / 2};
};

struct WakeVadEngine::SpeechVadRuntime {
  ~SpeechVadRuntime() {
    if (handle != nullptr) vad_destroy(handle);
  }

  bool initialize() {
    handle = vad_create_with_param(static_cast<vad_mode_t>(kVadMode),
                                   kVadSampleRateHz, kVadFrameMs,
                                   kVadMinimumSpeechMs, kVadMinimumNoiseMs);
    return handle != nullptr;
  }

  bool is_speech(const int16_t* pcm) const {
    // ESP-SR's C API predates const-correctness; it reads this audio frame.
    return vad_process(handle, const_cast<int16_t*>(pcm), kVadSampleRateHz,
                       kVadFrameMs) == VAD_SPEECH;
  }

  vad_handle_t handle{nullptr};
};

WakeVadEngine::~WakeVadEngine() { stop(); }

esp_err_t WakeVadEngine::start() {
  if (running_) return ESP_OK;
  wakeword_ = new (std::nothrow) WakeWordRuntime;
  speech_vad_ = new (std::nothrow) SpeechVadRuntime;
  if (wakeword_ == nullptr || speech_vad_ == nullptr ||
      !wakeword_->initialize() || !speech_vad_->initialize()) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  signal_queue_ = xQueueCreate(16, sizeof(WakeVadSignal));
  if (signal_queue_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  running_ = true;
  ESP_LOGI(kTag,
           "custom TFLite wake word + ESP-SR VAD mode 3 ready (start=60 ms, continuation=40 ms)");
  return ESP_OK;
}

void WakeVadEngine::stop() {
  running_ = false;
  if (signal_queue_ != nullptr) {
    vQueueDelete(signal_queue_);
    signal_queue_ = nullptr;
  }
  delete speech_vad_;
  speech_vad_ = nullptr;
  delete wakeword_;
  wakeword_ = nullptr;
}

esp_err_t WakeVadEngine::feed_pcm(const int16_t* pcm, size_t samples) {
  if (!running_ || wakeword_ == nullptr || pcm == nullptr || samples == 0) {
    return ESP_ERR_INVALID_STATE;
  }

  if (speech_vad_ == nullptr || samples != kVadSampleRateHz * kVadFrameMs / 1000) {
    return ESP_ERR_INVALID_SIZE;
  }

  const WakeVadSignal signal{
      .wake_detected = wakeword_->append_and_detect(pcm, samples),
      .vad_speech = speech_vad_->is_speech(pcm),
  };
  if (signal_queue_ != nullptr) xQueueSend(signal_queue_, &signal, 0);
  return ESP_OK;
}

bool WakeVadEngine::read_signal(WakeVadSignal* signal) {
  return signal != nullptr && signal_queue_ != nullptr &&
         xQueueReceive(signal_queue_, signal, 0) == pdTRUE;
}

}  // namespace sesame::voice
