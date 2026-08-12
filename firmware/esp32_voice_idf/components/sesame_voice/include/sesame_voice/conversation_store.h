#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace sesame::voice {

// Serializes conversation-id persistence on an internal-RAM task. The main
// voice task has a PSRAM stack for Opus and therefore must not call NVS: NVS
// temporarily disables the flash/PSRAM cache on ESP32-S3.
class ConversationStore final {
 public:
  static constexpr size_t kConversationIdCapacity = 101;
  static constexpr uint32_t kTaskStackBytes = 4096;

  ConversationStore() = default;
  ~ConversationStore();

  ConversationStore(const ConversationStore&) = delete;
  ConversationStore& operator=(const ConversationStore&) = delete;

  esp_err_t start();
  void stop();
  esp_err_t enqueue(const char* conversation_id);

 private:
  struct Request {
    std::array<char, kConversationIdCapacity> conversation_id{};
  };

  static void task_entry(void* context);
  void run();

  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
  std::atomic<bool> running_{false};
};

}  // namespace sesame::voice
