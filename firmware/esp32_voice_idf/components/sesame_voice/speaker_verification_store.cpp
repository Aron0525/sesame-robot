#include "sesame_voice/speaker_verification_store.h"

#include <cstdint>

#include "esp_log.h"

#include "sesame_transport/device_config.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "speaker_verify_nvs";

}  // namespace

SpeakerVerificationStore::~SpeakerVerificationStore() { stop(); }

esp_err_t SpeakerVerificationStore::start() {
  if (running_) return ESP_OK;
  queue_ = xQueueCreate(1, sizeof(uint8_t));
  if (queue_ == nullptr) return ESP_ERR_NO_MEM;
  running_ = true;
  if (xTaskCreatePinnedToCore(task_entry, "speaker_verify_nvs",
                              kTaskStackBytes, this, 4, &task_, 0) != pdPASS) {
    running_ = false;
    vQueueDelete(queue_);
    queue_ = nullptr;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void SpeakerVerificationStore::stop() {
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

esp_err_t SpeakerVerificationStore::enqueue(bool enabled) {
  if (!running_ || queue_ == nullptr) return ESP_ERR_INVALID_STATE;
  const uint8_t value = enabled ? 1 : 0;
  return xQueueOverwrite(queue_, &value) == pdPASS ? ESP_OK : ESP_ERR_TIMEOUT;
}

void SpeakerVerificationStore::task_entry(void* context) {
  auto* self = static_cast<SpeakerVerificationStore*>(context);
  self->run();
  self->task_ = nullptr;
  vTaskDelete(nullptr);
}

void SpeakerVerificationStore::run() {
  while (running_) {
    uint8_t value = 0;
    if (xQueueReceive(queue_, &value, pdMS_TO_TICKS(100)) != pdTRUE) continue;
    const bool enabled = value != 0;
    const esp_err_t result =
        sesame::transport::save_speaker_verification_enabled(enabled);
    if (result != ESP_OK) {
      ESP_LOGW(kTag, "could not persist speaker verification: %s",
               esp_err_to_name(result));
    } else {
      ESP_LOGI(kTag, "speaker verification saved: %s",
               enabled ? "enabled" : "disabled");
    }
  }
}

}  // namespace sesame::voice
