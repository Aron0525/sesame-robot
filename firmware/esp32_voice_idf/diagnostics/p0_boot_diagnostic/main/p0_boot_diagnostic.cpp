#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "sesame_p0";
constexpr uint32_t kRequiredStableSeconds = 60;

const char* reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power_on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
      return "task_watchdog";
    case ESP_RST_WDT:
      return "other_watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

}  // namespace

extern "C" void app_main() {
  esp_chip_info_t chip{};
  esp_chip_info(&chip);

  const esp_reset_reason_t reset_reason = esp_reset_reason();
  ESP_LOGI(kTag, "P0_BEGIN: isolated boot diagnostic");
  ESP_LOGI(kTag,
           "P0_BOARD: cores=%d revision=%d feature_mask=0x%" PRIx32
           " reset_reason=%s",
           chip.cores, chip.revision, chip.features,
           reset_reason_name(reset_reason));
  ESP_LOGI(kTag,
           "P0_SCOPE: serial + FreeRTOS heartbeat only; I2S/OLED/servo/Wi-Fi/"
           "PSRAM are intentionally disabled");

  TickType_t last_wake = xTaskGetTickCount();
  for (uint32_t second = 1;; ++second) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));

    const uint64_t uptime_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    ESP_LOGI(kTag,
             "P0_HEARTBEAT: second=%" PRIu32 " uptime_ms=%" PRIu64
             " heap_free=%" PRIu32 " heap_min=%" PRIu32,
             second, uptime_ms, esp_get_free_heap_size(),
             esp_get_minimum_free_heap_size());
    if (second == kRequiredStableSeconds) {
      ESP_LOGI(kTag,
               "P0_PASS: 60 seconds stable without initializing peripheral modules");
    }
  }
}
