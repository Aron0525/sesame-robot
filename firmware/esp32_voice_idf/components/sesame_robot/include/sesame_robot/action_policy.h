#pragma once

#include <cstdint>
#include <string_view>

namespace sesame::robot {

struct ActionRequest {
  std::string_view request_id;
  std::string_view action;
  uint32_t duration_ms;
  uint64_t deadline_ms;
};

enum class ActionDecision {
  kAllowed = 0,
  kInvalidRequestId,
  kUnknownAction,
  kDurationOutOfRange,
  kExpired,
  kUnsafeState,
};

enum class ExpressionDecision {
  kAllowed = 0,
  kUnknownExpression,
  kTtlOutOfRange,
};

ActionDecision validate_action(const ActionRequest& request, uint64_t now_ms,
                               bool robot_safe);
ExpressionDecision validate_expression(std::string_view expression,
                                       uint32_t ttl_ms);

}  // namespace sesame::robot
