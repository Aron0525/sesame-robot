#include "sesame_robot/robot_adapter.h"

#include <cstring>

namespace sesame::robot {
namespace {

bool is_operator_action_allowed(std::string_view action) {
  return action == "rest" || action == "stand" || action == "wave" ||
         action == "dance" || action == "swim" || action == "point" ||
         action == "pushup" || action == "bow" || action == "cute" ||
         action == "freaky" || action == "worm" || action == "shake" ||
         action == "shrug" || action == "dead" || action == "crab" ||
         action == "forward" || action == "backward" || action == "left" ||
         action == "right";
}

}  // namespace

ActionDecision RobotAdapter::execute(const ActionRequest& request,
                                     uint64_t now_ms) {
  const bool safe = driver_ != nullptr && driver_->safe_for_motion();
  const ActionDecision decision = validate_action(request, now_ms, safe);
  if (decision != ActionDecision::kAllowed) return decision;
  if (request.action == "stop") {
    emergency_stop();
    return ActionDecision::kAllowed;
  }
  return driver_->execute_action(request.action.data(), request.duration_ms)
             ? ActionDecision::kAllowed
             : ActionDecision::kUnsafeState;
}

bool RobotAdapter::execute_operator_action(const char* action) {
  if (driver_ == nullptr || action_executor_ == nullptr || action == nullptr ||
      !is_operator_action_allowed(action)) {
    return false;
  }
  // The legacy runner performs the ownership transition by calling
  // begin_web_motion(). Checking safe_for_motion() here prevents that
  // transition after emergency_stop(), leaving remote control permanently
  // latched off after the first WSS reconnect.
  return action_executor_(action_executor_context_, action);
}

bool RobotAdapter::set_operator_expression(const char* expression) {
  // The OLED driver is the source of truth for the full face catalog. Keep the
  // narrower policy below for model-generated response plans only.
  return expression != nullptr && driver_ != nullptr &&
         driver_->set_expression(expression, 10000);
}

bool RobotAdapter::set_manual_servo(uint8_t servo_number, uint8_t angle) {
  if (driver_ == nullptr || servo_number < 1 || servo_number > 8 ||
      angle > 180) {
    return false;
  }
  if (before_driver_stop_ != nullptr) before_driver_stop_(hook_context_);
  return driver_->begin_manual_control() &&
         driver_->set_manual_angle(servo_number - 1, angle);
}

void RobotAdapter::set_action_executor(ActionExecutor action_executor,
                                       void* context) {
  action_executor_ = action_executor;
  action_executor_context_ = context;
}

void RobotAdapter::set_motion_settings_executor(
    MotionSettingsExecutor settings_executor, void* context) {
  settings_executor_ = settings_executor;
  settings_executor_context_ = context;
}

bool RobotAdapter::configure_motion(int frame_delay_ms, int walk_cycles,
                                    int motor_current_delay_ms) {
  return settings_executor_ != nullptr &&
         settings_executor_(settings_executor_context_, frame_delay_ms,
                            walk_cycles, motor_current_delay_ms);
}

ExpressionDecision RobotAdapter::set_expression(const char* expression,
                                                uint32_t ttl_ms) {
  if (expression == nullptr) return ExpressionDecision::kUnknownExpression;
  const ExpressionDecision decision =
      validate_expression(expression, ttl_ms);
  if (decision != ExpressionDecision::kAllowed) return decision;
  return driver_ != nullptr &&
                 driver_->set_expression(expression, ttl_ms)
             ? ExpressionDecision::kAllowed
             : ExpressionDecision::kUnknownExpression;
}

void RobotAdapter::emergency_stop() {
  if (before_driver_stop_ != nullptr) before_driver_stop_(hook_context_);
  if (driver_ != nullptr) driver_->emergency_stop();
}

}  // namespace sesame::robot
