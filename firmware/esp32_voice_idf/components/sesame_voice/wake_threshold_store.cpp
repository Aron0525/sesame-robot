#include "sesame_voice/wake_threshold_store.h"

#include "esp_log.h"

#include "sesame_transport/device_config.h"
#include "sesame_voice/wake_capture_policy.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "wake_threshold_nvs";

}  // namespace

WakeThresholdStore::~WakeThresholdStore() { stop(); }

esp_err_t WakeThresholdStore::start() {
  if (running_) return ESP_OK;
  queue_ = xQueueCreate(1, sizeof(uint8_t));
  if (queue_ == nullptr) return ESP_ERR_NO_MEM;
  running_ = true;
  if (xTaskCreatePinnedToCore(task_entry, "wake_threshold_nvs",
                              kTaskStackBytes, this, 4, &task_, 0) != pdPASS) {
    running_ = false;
    vQueueDelete(queue_);
    queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void WakeThresholdStore::stop() {
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

esp_err_t WakeThresholdStore::enqueue(uint8_t threshold_hundredths) {
  if (!running_ || queue_ == nullptr) return ESP_ERR_INVALID_STATE;
  if (!is_valid_wake_threshold_hundredths(threshold_hundredths)) {
    return ESP_ERR_INVALID_ARG;
  }
  return xQueueOverwrite(queue_, &threshold_hundredths) == pdPASS
             ? ESP_OK
             : ESP_ERR_TIMEOUT;
}

void WakeThresholdStore::task_entry(void* context) {
  auto* self = static_cast<WakeThresholdStore*>(context);
  self->run();
  self->task_ = nullptr;
  vTaskDelete(nullptr);
}

void WakeThresholdStore::run() {
  while (running_) {
    uint8_t threshold_hundredths = 0;
    if (xQueueReceive(queue_, &threshold_hundredths, pdMS_TO_TICKS(100)) !=
        pdTRUE) {
      continue;
    }
    const esp_err_t result = sesame::transport::save_wake_threshold_hundredths(
        threshold_hundredths);
    if (result != ESP_OK) {
      ESP_LOGW(kTag, "could not persist wake threshold %u: %s",
               static_cast<unsigned>(threshold_hundredths),
               esp_err_to_name(result));
    } else {
      ESP_LOGI(kTag, "wake threshold saved: %.2f",
               threshold_hundredths / 100.0f);
    }
  }
}

}  // namespace sesame::voice
