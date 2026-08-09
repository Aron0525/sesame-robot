#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace sesame::robot {
class Esp32ServoDriver;
}

namespace sesame::ui {
class OledExpressionDisplay;
enum class FaceAnimationMode : uint8_t;
}

namespace sesame::web {

// Runs the original web-page movements on a dedicated task. During a movement
// the voice controller is deliberately paused from driving the same servos.
class LegacyMotionRunner {
 public:
  LegacyMotionRunner(robot::Esp32ServoDriver* servos,
                     ui::OledExpressionDisplay* display);

  // Web requests use action-owned faces; voice plans preserve the separately
  // selected expression while running the exact same servo sequence.
  bool start(std::string_view action, bool show_action_face = true);
  void stop();
  [[nodiscard]] bool busy() const;
  [[nodiscard]] std::string_view active_action() const;

  bool set_settings(int frame_delay_ms, int walk_cycles,
                    int motor_current_delay_ms);
  [[nodiscard]] int frame_delay_ms() const;
  [[nodiscard]] int walk_cycles() const;
  [[nodiscard]] int motor_current_delay_ms() const;

  bool set_servo_angle(uint8_t servo_index, uint8_t angle);
  void set_face(
      std::string_view expression,
      ui::FaceAnimationMode mode = static_cast<ui::FaceAnimationMode>(0));
  void enter_idle();
  [[nodiscard]] bool should_continue(std::string_view action,
                                     uint32_t duration_ms) const;

 private:
  static void task_entry(void* context);
  void run();
  void execute(std::string_view action);

  robot::Esp32ServoDriver* servos_{nullptr};
  ui::OledExpressionDisplay* display_{nullptr};
  std::array<char, 20> active_action_{};
  std::atomic<bool> busy_{false};
  std::atomic<bool> cancel_requested_{false};
  std::atomic<bool> show_action_face_{true};
  TaskHandle_t task_{nullptr};
};

}  // namespace sesame::web
