#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_afe_sr_iface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "model_path.h"

namespace sesame::voice {

struct WakeVadSignal {
  bool wake_detected;
  bool vad_speech;
};

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
  static void fetch_task_entry(void* context);
  void fetch_loop();

  static constexpr size_t kMaxFeedSamples = 2048;
  const esp_afe_sr_iface_t* afe_handle_{nullptr};
  esp_afe_sr_data_t* afe_data_{nullptr};
  srmodel_list_t* models_{nullptr};
  QueueHandle_t signal_queue_{nullptr};
  TaskHandle_t fetch_task_{nullptr};
  std::atomic<bool> running_{false};
  std::array<int16_t, kMaxFeedSamples> feed_buffer_{};
  size_t buffered_samples_{0};
  int feed_chunk_samples_{0};
};

}  // namespace sesame::voice
