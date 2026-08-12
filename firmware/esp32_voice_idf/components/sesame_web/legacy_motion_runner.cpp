#include "sesame_web/legacy_motion_runner.h"

#include <algorithm>
#include <cstring>

#include "Arduino.h"
#include "esp_log.h"

#include "sesame_robot/esp32_servo_driver.h"
#include "sesame_robot/servo_calibration.h"
#include "sesame_ui/oled_expression_display.h"

// The migrated movement table was originally written for Arduino globals.
// Keep that narrow compatibility seam here rather than duplicating or
// reinterpreting any of the tested servo sequences.
#include "sesame_web/legacy_movement_sequences.h"

int frameDelay = 100;
int walkCycles = 10;
int motorCurrentDelay = 20;
String currentCommand;

namespace {

constexpr char kTag[] = "sesame_web_motion";
// Movement routines use small fixed locals; 4 KiB fits in the available internal
// heap after voice/WakeNet startup, whereas the previous 6 KiB task could fail.
constexpr uint32_t kMotionTaskStackBytes = 4096;
sesame::web::LegacyMotionRunner* g_runner = nullptr;

sesame::ui::FaceAnimationMode g_face_mode =
    sesame::ui::FaceAnimationMode::kLoop;

sesame::ui::FaceAnimationMode to_display_face_mode(FaceAnimMode mode) {
  switch (mode) {
    case FACE_ANIM_ONCE:
      return sesame::ui::FaceAnimationMode::kOnce;
    case FACE_ANIM_BOOMERANG:
      return sesame::ui::FaceAnimationMode::kBoomerang;
    case FACE_ANIM_LOOP:
    default:
      return sesame::ui::FaceAnimationMode::kLoop;
  }
}

}  // namespace

void setServoAngle(uint8_t channel, int angle) {
  if (g_runner == nullptr || angle < 0 || angle > 180) return;
  g_runner->set_servo_angle(channel, static_cast<uint8_t>(angle));
}

void setFace(const String& face_name) {
  if (g_runner != nullptr) g_runner->set_face(face_name.c_str(), g_face_mode);
}

void setFaceMode(FaceAnimMode mode) { g_face_mode = to_display_face_mode(mode); }

void setFaceWithMode(const String& face_name, FaceAnimMode mode) {
  setFaceMode(mode);
  setFace(face_name);
}

void delayWithFace(unsigned long milliseconds) {
  if (g_runner != nullptr) {
    static_cast<void>(
        g_runner->should_continue(currentCommand.c_str(), milliseconds));
  }
}

void enterIdle() {
  if (g_runner != nullptr) g_runner->enter_idle();
}

bool pressingCheck(String command, int milliseconds) {
  return g_runner != nullptr &&
         g_runner->should_continue(command.c_str(), milliseconds);
}

namespace sesame::web {
namespace {

template <size_t N>
void copy_string(std::array<char, N>* destination, std::string_view value) {
  const size_t size = std::min(value.size(), destination->size() - 1);
  std::memcpy(destination->data(), value.data(), size);
  (*destination)[size] = '\0';
}

}  // namespace

LegacyMotionRunner::LegacyMotionRunner(robot::Esp32ServoDriver* servos,
                                       ui::OledExpressionDisplay* display)
    : servos_(servos), display_(display) {
  g_runner = this;
}

bool LegacyMotionRunner::start(std::string_view action, bool show_action_face) {
  if (servos_ == nullptr) {
    ESP_LOGW(kTag, "web movement rejected: servo driver unavailable");
    return false;
  }
  if (action.empty()) {
    ESP_LOGW(kTag, "web movement rejected: empty action");
    return false;
  }
  if (busy_.exchange(true)) {
    ESP_LOGW(kTag, "web movement rejected: runner busy (action=%.*s)",
             static_cast<int>(action.size()), action.data());
    return false;
  }

  copy_string(&active_action_, action);
  cancel_requested_.store(false);
  show_action_face_.store(show_action_face);
  if (display_ != nullptr) display_->exit_idle();
  if (!servos_->begin_web_motion()) {
    ESP_LOGW(kTag, "web movement rejected: servo driver not running");
    busy_.store(false);
    active_action_[0] = '\0';
    return false;
  }

  if (xTaskCreatePinnedToCore(task_entry, "sesame_web_motion",
                              kMotionTaskStackBytes, this, 5,
                              &task_, 1) != pdPASS) {
    ESP_LOGW(kTag, "web movement rejected: task creation failed");
    servos_->end_web_control(true);
    busy_.store(false);
    active_action_[0] = '\0';
    return false;
  }
  return true;
}

void LegacyMotionRunner::stop() {
  cancel_requested_.store(true);
  currentCommand = "";
  if (servos_ != nullptr) servos_->end_web_control(true);
}

bool LegacyMotionRunner::busy() const { return busy_.load(); }

std::string_view LegacyMotionRunner::active_action() const {
  return active_action_.data();
}

bool LegacyMotionRunner::set_settings(int frame_delay_ms, int walk_cycles,
                                      int motor_current_delay_ms) {
  if (busy() || frame_delay_ms < 10 || frame_delay_ms > 1000 ||
      walk_cycles < 1 || walk_cycles > 50 || motor_current_delay_ms < 0 ||
      motor_current_delay_ms > 500) {
    return false;
  }
  frameDelay = frame_delay_ms;
  walkCycles = walk_cycles;
  motorCurrentDelay = motor_current_delay_ms;
  return true;
}

int LegacyMotionRunner::frame_delay_ms() const { return frameDelay; }

int LegacyMotionRunner::walk_cycles() const { return walkCycles; }

int LegacyMotionRunner::motor_current_delay_ms() const {
  return motorCurrentDelay;
}

bool LegacyMotionRunner::set_servo_angle(uint8_t servo_index, uint8_t angle) {
  const bool preserve_r3_l4_full_range =
      robot::preserves_r3_l4_full_range_for_action(active_action());
  if (cancel_requested_.load() || servos_ == nullptr ||
      !servos_->set_motion_angle(servo_index, angle,
                                 preserve_r3_l4_full_range)) {
    return false;
  }
  // Preserve the verified Arduino timing: staggering each channel by this
  // bounded delay avoids the eight servos drawing their peak current at once.
  return should_continue(active_action(),
                         static_cast<uint32_t>(motorCurrentDelay));
}

void LegacyMotionRunner::set_face(std::string_view expression,
                                  ui::FaceAnimationMode mode) {
  if (show_action_face_.load() && display_ != nullptr) {
    display_->show_expression(expression, mode);
  }
}

void LegacyMotionRunner::enter_idle() {
  if (show_action_face_.load() && display_ != nullptr) display_->enter_idle();
}

bool LegacyMotionRunner::should_continue(std::string_view action,
                                         uint32_t duration_ms) const {
  constexpr uint32_t kSliceMs = 20;
  for (uint32_t elapsed = 0; elapsed < duration_ms; elapsed += kSliceMs) {
    if (cancel_requested_.load() || !busy_.load() ||
        action != active_action()) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(std::min(kSliceMs, duration_ms - elapsed)));
  }
  return !cancel_requested_.load() && action == active_action();
}

void LegacyMotionRunner::task_entry(void* context) {
  static_cast<LegacyMotionRunner*>(context)->run();
}

void LegacyMotionRunner::run() {
  currentCommand = active_action_.data();
  execute(active_action());

  const bool cancelled = cancel_requested_.load();
  currentCommand = "";
  active_action_[0] = '\0';
  busy_.store(false);
  show_action_face_.store(true);
  task_ = nullptr;
  if (!cancelled && servos_ != nullptr) servos_->end_web_control(false);
  ESP_LOGI(kTag, "web movement finished%s", cancelled ? " after stop" : "");
  vTaskDelete(nullptr);
}

void LegacyMotionRunner::execute(std::string_view action) {
  if (action == "rest") {
    runRestPose();
  } else if (action == "stand") {
    runStandPose();
  } else if (action == "wave") {
    runWavePose();
  } else if (action == "dance") {
    runDancePose();
  } else if (action == "swim") {
    runSwimPose();
  } else if (action == "point") {
    runPointPose();
  } else if (action == "pushup") {
    runPushupPose();
  } else if (action == "bow") {
    runBowPose();
  } else if (action == "cute") {
    runCutePose();
  } else if (action == "freaky") {
    runFreakyPose();
  } else if (action == "worm") {
    runWormPose();
  } else if (action == "shake") {
    runShakePose();
  } else if (action == "shrug") {
    runShrugPose();
  } else if (action == "dead") {
    runDeadPose();
  } else if (action == "crab") {
    runCrabPose();
  } else if (action == "forward") {
    runWalkPose();
  } else if (action == "backward") {
    runWalkBackward();
  } else if (action == "left") {
    runTurnLeft();
  } else if (action == "right") {
    runTurnRight();
  }
}

}  // namespace sesame::web
