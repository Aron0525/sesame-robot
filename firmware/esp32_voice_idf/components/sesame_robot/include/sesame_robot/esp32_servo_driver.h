#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sesame_robot/motion_executor.h"
#include "sesame_robot/robot_adapter.h"

namespace sesame::ui {
class OledExpressionDisplay;
}

namespace sesame::robot {

class Esp32ServoDriver final : public RobotDriver, public MotionOutput {
 public:
  explicit Esp32ServoDriver(sesame::ui::OledExpressionDisplay* display);

  esp_err_t initialize();

  bool safe_for_motion() const override;
  bool execute_action(const char* action, uint32_t duration_ms) override;
  bool set_expression(const char* expression, uint32_t ttl_ms) override;
  void emergency_stop() override;

  // Web movements and manual sliders share the physical PWM outputs, but they
  // have different lifetime semantics. A movement owns the outputs only for
  // its task; a manual slider keeps its selected pulse until an explicit Stop
  // or a safety takeover releases it.
  bool begin_web_motion();
  bool begin_manual_control();
  void end_web_control(bool release_outputs);
  bool set_motion_angle(uint8_t servo_index, uint8_t angle);
  bool set_manual_angle(uint8_t servo_index, uint8_t angle);

  void apply_pose(const std::array<uint8_t, 8>& angles) override;
  void release_all() override;

  enum class MotionId : uint8_t { kRest, kStand, kWave };

 private:
  static void task_entry(void* context);
  void run();
  void apply_manual_pose();
  bool write_logical_angle(uint8_t servo_index, uint8_t logical_angle);
  static const char* action_name(MotionId action);
  static uint32_t duty_for_angle(uint8_t angle);

  QueueHandle_t command_queue_{nullptr};
  TaskHandle_t task_{nullptr};
  MotionExecutor executor_;
  std::atomic<bool> pwm_configured_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> web_control_active_{false};
  std::atomic<bool> manual_control_active_{false};
  std::array<std::atomic<uint8_t>, 8> manual_angles_{};
  std::array<std::atomic<bool>, 8> manual_angle_valid_{};
  sesame::ui::OledExpressionDisplay* display_{nullptr};
};

}  // namespace sesame::robot
