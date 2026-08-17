#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace sesame::voice {

// NVS writes must not run on the PSRAM-backed voice task, because flash cache
// operations temporarily make external memory unavailable on ESP32-S3.
class WakeThresholdStore final {
 public:
  static constexpr uint32_t kTaskStackBytes = 4096;

  WakeThresholdStore() = default;
  ~WakeThresholdStore();
  WakeThresholdStore(const WakeThresholdStore&) = delete;
  WakeThresholdStore& operator=(const WakeThresholdStore&) = delete;

  esp_err_t start();
  void stop();
  esp_err_t enqueue(uint8_t threshold_hundredths);

 private:
  static void task_entry(void* context);
  void run();

  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
  std::atomic<bool> running_{false};
};

}  // namespace sesame::voice
