#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace sesame::voice {

struct WakeVadSignal {
  bool wake_detected;
  bool vad_speech;
};

// Runs the embedded 你好芝麻 TFLite model and ESP-SR VAD.
// The VAD is a prebuilt ESP-SR runtime; it does not add a separate model file.
class WakeVadEngine final {
 public:
  WakeVadEngine() = default;
  ~WakeVadEngine();

  WakeVadEngine(const WakeVadEngine&) = delete;
  WakeVadEngine& operator=(const WakeVadEngine&) = delete;

  esp_err_t start();
  void stop();
  esp_err_t feed_pcm(const int16_t* pcm, size_t samples);
  bool read_signal(WakeVadSignal* signal);

 private:
  struct WakeWordRuntime;
  struct SpeechVadRuntime;

  QueueHandle_t signal_queue_{nullptr};
  std::atomic<bool> running_{false};
  WakeWordRuntime* wakeword_{nullptr};
  SpeechVadRuntime* speech_vad_{nullptr};
};

}  // namespace sesame::voice
