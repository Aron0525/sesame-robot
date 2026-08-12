#include "sesame_voice/conversation_store.h"

#include <cstring>

#include "esp_log.h"

#include "sesame_transport/device_config.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "conversation_store";

}  // namespace

ConversationStore::~ConversationStore() { stop(); }

esp_err_t ConversationStore::start() {
  if (running_) return ESP_OK;
  queue_ = xQueueCreate(1, sizeof(Request));
  if (queue_ == nullptr) return ESP_ERR_NO_MEM;

  running_ = true;
  // This must remain a normal internal-RAM task. A PSRAM stack would assert
  // as soon as save_conversation_id() asks NVS to disable the flash cache.
  if (xTaskCreatePinnedToCore(task_entry, "conversation_nvs", kTaskStackBytes,
                              this, 4, &task_, 0) != pdPASS) {
    running_ = false;
    vQueueDelete(queue_);
    queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void ConversationStore::stop() {
  running_ = false;
  if (task_ != nullptr) {
    for (int attempt = 0; attempt < 20 && task_ != nullptr; ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (task_ != nullptr) {
      vTaskDelete(task_);
      task_ = nullptr;
    }
  }
  if (queue_ != nullptr) {
    vQueueDelete(queue_);
    queue_ = nullptr;
  }
}

esp_err_t ConversationStore::enqueue(const char* conversation_id) {
  if (!running_ || queue_ == nullptr || conversation_id == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  const size_t size = strnlen(conversation_id, kConversationIdCapacity);
  if (size == 0 || size >= kConversationIdCapacity) {
    return ESP_ERR_INVALID_ARG;
  }

  Request request{};
  std::memcpy(request.conversation_id.data(), conversation_id, size);
  request.conversation_id[size] = '\0';
  // Only the newest validated identifier matters across reboots. Overwrite a
  // pending request if reconnects arrive faster than one NVS commit.
  return xQueueOverwrite(queue_, &request) == pdPASS ? ESP_OK
                                                      : ESP_ERR_TIMEOUT;
}

void ConversationStore::task_entry(void* context) {
  auto* self = static_cast<ConversationStore*>(context);
  self->run();
  self->task_ = nullptr;
  vTaskDelete(nullptr);
}

void ConversationStore::run() {
  while (running_) {
    Request request{};
    if (xQueueReceive(queue_, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }
    const esp_err_t result = sesame::transport::save_conversation_id(
        request.conversation_id.data());
    if (result != ESP_OK) {
      ESP_LOGW(kTag, "could not persist conversation identifier: %s",
               esp_err_to_name(result));
    } else {
      ESP_LOGI(kTag, "conversation identifier persisted; stack free=%u bytes",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
  }
}

}  // namespace sesame::voice
