#include "sesame_robot/robot_adapter.h"

#include <cstring>

namespace sesame::robot {

ActionDecision RobotAdapter::execute(const ActionRequest& request,
                                     uint64_t now_ms) {
  const bool safe = driver_ != nullptr && driver_->safe_for_motion();
  const ActionDecision decision = validate_action(request, now_ms, safe);
  if (decision != ActionDecision::kAllowed) return decision;
  if (request.action == "stop") {
    driver_->emergency_stop();
    return ActionDecision::kAllowed;
  }
  return driver_->execute_action(request.action.data(), request.duration_ms)
             ? ActionDecision::kAllowed
             : ActionDecision::kUnsafeState;
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
