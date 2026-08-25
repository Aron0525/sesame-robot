#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace sesame::ui {

enum class FaceAnimationMode : uint8_t {
  kLoop,
  kOnce,
  kBoomerang,
};

class OledExpressionDisplay {
 public:
  esp_err_t initialize();
  bool show_expression(std::string_view expression,
                       FaceAnimationMode mode = FaceAnimationMode::kLoop);
  bool set_animation_mode(FaceAnimationMode mode);
  bool enter_idle();
  void exit_idle();
  [[nodiscard]] std::string_view current_expression() const;
  [[nodiscard]] size_t expression_count() const;
  [[nodiscard]] std::string_view expression_name(size_t index) const;

 private:
  static constexpr size_t kWidth = 128;
  static constexpr size_t kHeight = 64;
  static constexpr size_t kBitmapBytes = (kWidth * kHeight) / 8;

  esp_err_t send_command(uint8_t command);
  esp_err_t write_bitmap(const uint8_t* source);
  bool set_expression_locked(std::string_view expression,
                             FaceAnimationMode mode, bool keep_idle);
  void tick_animation();
  void schedule_idle_blink_locked(uint32_t minimum_ms, uint32_t maximum_ms);
  static void animation_task_entry(void* context);

  i2c_master_bus_handle_t bus_{nullptr};
  i2c_master_dev_handle_t device_{nullptr};
  SemaphoreHandle_t mutex_{nullptr};
  std::array<char, 24> current_expression_{};
  std::array<const uint8_t*, 6> current_frames_{};
  uint8_t current_frame_count_{0};
  uint8_t current_frame_index_{0};
  uint8_t current_fps_{1};
  int8_t frame_direction_{1};
  uint32_t last_frame_ms_{0};
  uint32_t next_idle_blink_ms_{0};
  uint8_t idle_blinks_remaining_{0};
  FaceAnimationMode animation_mode_{FaceAnimationMode::kLoop};
  bool animation_finished_{false};
  bool idle_active_{false};
  bool idle_blink_active_{false};
  TaskHandle_t animation_task_{nullptr};
  bool initialized_{false};
};

}  // namespace sesame::ui
