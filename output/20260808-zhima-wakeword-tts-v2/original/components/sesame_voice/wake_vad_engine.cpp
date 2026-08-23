// WakeNet/AFE integration adapted from Espressif's CC0 wake_word_detection AFE
// example. This project keeps its own I2S driver, turn state machine and WSS
// protocol; only the AFE feed/fetch integration pattern is reused.

#include "sesame_voice/wake_vad_engine.h"

#include <cstring>

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "model_path.h"

namespace sesame::voice {
namespace {

constexpr char kTag[] = "wake_vad";
constexpr char kModelPartition[] = "model";

}  // namespace

WakeVadEngine::~WakeVadEngine() { stop(); }

esp_err_t WakeVadEngine::start() {
  if (running_) return ESP_OK;

  models_ = esp_srmodel_init(kModelPartition);
  if (models_ == nullptr) {
    ESP_LOGE(kTag, "no ESP-SR models in partition '%s'", kModelPartition);
    return ESP_ERR_NOT_FOUND;
  }

  afe_config_t* config =
      afe_config_init("M", models_, AFE_TYPE_SR, AFE_MODE_LOW_COST);
  if (config == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  config->vad_init = true;
  config->vad_min_speech_ms = 128;
  config->vad_min_noise_ms = 800;
  config->vad_delay_ms = 128;

  afe_handle_ = esp_afe_handle_from_config(config);
  if (afe_handle_ != nullptr) {
    afe_data_ = afe_handle_->create_from_config(config);
  }
  afe_config_free(config);
  if (afe_handle_ == nullptr || afe_data_ == nullptr) {
    stop();
    return ESP_FAIL;
  }

  feed_chunk_samples_ = afe_handle_->get_feed_chunksize(afe_data_);
  if (feed_chunk_samples_ <= 0 ||
      static_cast<size_t>(feed_chunk_samples_) > kMaxFeedSamples) {
    stop();
    return ESP_ERR_INVALID_SIZE;
  }
  signal_queue_ = xQueueCreate(8, sizeof(WakeVadSignal));
  if (signal_queue_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }

  running_ = true;
  if (xTaskCreatePinnedToCore(fetch_task_entry, "wake_vad_fetch", 6144, this,
                              6, &fetch_task_, 0) != pdPASS) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  afe_handle_->print_pipeline(afe_data_);
  ESP_LOGI(kTag, "WakeNet + VAD test pipeline ready; feed frame=%d samples",
           feed_chunk_samples_);
  return ESP_OK;
}

void WakeVadEngine::stop() {
  running_ = false;
  if (fetch_task_ != nullptr) {
    for (int attempt = 0; attempt < 50 && fetch_task_ != nullptr; ++attempt) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (fetch_task_ != nullptr) {
      vTaskDelete(fetch_task_);
      fetch_task_ = nullptr;
    }
  }
  if (signal_queue_ != nullptr) {
    vQueueDelete(signal_queue_);
    signal_queue_ = nullptr;
  }
  if (afe_handle_ != nullptr && afe_data_ != nullptr) {
    afe_handle_->destroy(afe_data_);
  }
  afe_data_ = nullptr;
  afe_handle_ = nullptr;
  if (models_ != nullptr) {
    esp_srmodel_deinit(models_);
    models_ = nullptr;
  }
  buffered_samples_ = 0;
  feed_chunk_samples_ = 0;
}

esp_err_t WakeVadEngine::feed_pcm(const int16_t* pcm, size_t samples) {
  if (!running_ || pcm == nullptr || feed_chunk_samples_ <= 0) {
    return ESP_ERR_INVALID_STATE;
  }
  if (samples > kMaxFeedSamples - buffered_samples_) {
    return ESP_ERR_INVALID_SIZE;
  }
  std::memcpy(feed_buffer_.data() + buffered_samples_, pcm,
              samples * sizeof(int16_t));
  buffered_samples_ += samples;

  const size_t chunk = static_cast<size_t>(feed_chunk_samples_);
  while (buffered_samples_ >= chunk) {
    if (afe_handle_->feed(afe_data_, feed_buffer_.data()) <= 0) {
      return ESP_FAIL;
    }
    buffered_samples_ -= chunk;
    if (buffered_samples_ > 0) {
      std::memmove(feed_buffer_.data(), feed_buffer_.data() + chunk,
                   buffered_samples_ * sizeof(int16_t));
    }
  }
  return ESP_OK;
}

bool WakeVadEngine::read_signal(WakeVadSignal* signal) {
  return signal != nullptr && signal_queue_ != nullptr &&
         xQueueReceive(signal_queue_, signal, 0) == pdTRUE;
}

void WakeVadEngine::fetch_task_entry(void* context) {
  auto* self = static_cast<WakeVadEngine*>(context);
  self->fetch_loop();
  self->fetch_task_ = nullptr;
  vTaskDelete(nullptr);
}

void WakeVadEngine::fetch_loop() {
  while (running_) {
    afe_fetch_result_t* result = afe_handle_->fetch(afe_data_);
    if (!running_) return;
    if (result == nullptr || result->ret_value == ESP_FAIL) {
      ESP_LOGW(kTag, "AFE fetch failed");
      continue;
    }
    const WakeVadSignal signal{
        .wake_detected = result->wakeup_state == WAKENET_DETECTED,
        .vad_speech = result->vad_state == VAD_SPEECH,
    };
    if (signal_queue_ != nullptr) {
      xQueueSend(signal_queue_, &signal, 0);
    }
  }
}

}  // namespace sesame::voice
