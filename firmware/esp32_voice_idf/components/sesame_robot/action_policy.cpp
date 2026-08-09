#include "sesame_robot/action_policy.h"

#include "sesame_robot/control_catalog.h"

namespace sesame::robot {
namespace {

bool is_action_allowed(std::string_view action) {
  return action == "stop" || is_web_action(action);
}

bool is_expression_allowed(std::string_view expression) {
  return expression == "default" || is_web_expression(expression);
}

}  // namespace

ActionDecision validate_action(const ActionRequest& request, uint64_t now_ms,
                               bool robot_safe) {
  if (request.request_id.empty()) return ActionDecision::kInvalidRequestId;
  if (!is_action_allowed(request.action)) {
    return ActionDecision::kUnknownAction;
  }
  if (request.duration_ms == 0 || request.duration_ms > 5000) {
    return ActionDecision::kDurationOutOfRange;
  }
  if (request.deadline_ms < now_ms) return ActionDecision::kExpired;
  if (!robot_safe && request.action != "stop") {
    return ActionDecision::kUnsafeState;
  }
  return ActionDecision::kAllowed;
}

ExpressionDecision validate_expression(std::string_view expression,
                                       uint32_t ttl_ms) {
  if (!is_expression_allowed(expression)) {
    return ExpressionDecision::kUnknownExpression;
  }
  if (ttl_ms < 100 || ttl_ms > 10000) {
    return ExpressionDecision::kTtlOutOfRange;
  }
  return ExpressionDecision::kAllowed;
}

}  // namespace sesame::robot
