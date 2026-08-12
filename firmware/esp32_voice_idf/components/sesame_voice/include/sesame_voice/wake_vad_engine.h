#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sesame_audio/audio_contract.h"

namespace sesame::voice {

struct WakeVadSignal {
  bool wake_detected;
  bool vad_speech;
};

// Runs XiaoZhi-style ESP-SR AFE VAD plus MultiNet command recognition. The
// `ni hao zhi ma` phrase is provided to MultiNet at startup; no user PCM is
// used to train or embed a custom neural model in the application binary.
class WakeVadEngine final {
 public:
  WakeVadEngine() = default;
  ~WakeVadEngine();

  WakeVadEngine(const WakeVadEngine&) = delete;
  WakeVadEngine& operator=(const WakeVadEngine&) = delete;

  esp_err_t start();
  void stop();
  // MultiNet is armed while idle and during TTS barge-in. VAD continues while
  // it is disarmed so endpoints and the follow-up gate remain independent.
  void set_wake_enabled(bool enabled);
  esp_err_t feed_pcm(const int16_t* pcm, size_t samples);
  bool read_signal(WakeVadSignal* signal);

 private:
  struct WakeWordRuntime;
  struct SpeechVadRuntime;
  struct WakeAudioFrame {
    std::array<int16_t, sesame::audio::kSamplesPerFrame> samples{};
  };

  static void processing_task_entry(void* context);
  void processing_loop();

  static constexpr UBaseType_t kAudioQueueDepth = 32;
  static constexpr UBaseType_t kSignalQueueDepth = 64;
  QueueHandle_t audio_queue_{nullptr};
  QueueHandle_t signal_queue_{nullptr};
  TaskHandle_t processing_task_{nullptr};
  std::atomic<bool> running_{false};
  std::atomic<bool> wake_enabled_{true};
  std::atomic<bool> wake_reset_requested_{false};
  std::atomic<uint32_t> dropped_audio_frames_{0};
  WakeWordRuntime* wakeword_{nullptr};
};

}  // namespace sesame::voice
