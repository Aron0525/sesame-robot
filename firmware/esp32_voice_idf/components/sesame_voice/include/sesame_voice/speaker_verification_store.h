#pragma once

#include <atomic>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace sesame::voice {

// Persists the feature switch from an internal-RAM task. ESP32-S3 flash writes
// temporarily disable the PSRAM cache, so the voice task must only enqueue.
class SpeakerVerificationStore final {
 public:
  static constexpr uint32_t kTaskStackBytes = 4096;

  SpeakerVerificationStore() = default;
  ~SpeakerVerificationStore();
  SpeakerVerificationStore(const SpeakerVerificationStore&) = delete;
  SpeakerVerificationStore& operator=(const SpeakerVerificationStore&) = delete;

  esp_err_t start();
  void stop();
  esp_err_t enqueue(bool enabled);

 private:
  static void task_entry(void* context);
  void run();

  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
  std::atomic<bool> running_{false};
};

}  // namespace sesame::voice
