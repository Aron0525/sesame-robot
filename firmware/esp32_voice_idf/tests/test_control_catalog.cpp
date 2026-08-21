#include <cassert>

#include "sesame_robot/action_policy.h"
#include "sesame_robot/control_catalog.h"

int main() {
  constexpr uint64_t kNowMs = 1'000;

  for (const std::string_view action : sesame::robot::kWebActions) {
    const sesame::robot::ActionRequest request{
        "req_catalog", action, 1'000, kNowMs};
    assert(sesame::robot::validate_action(request, kNowMs, true) ==
           sesame::robot::ActionDecision::kAllowed);
  }

  for (const std::string_view expression : sesame::robot::kWebExpressions) {
    assert(sesame::robot::validate_expression(expression, 1'000) ==
           sesame::robot::ExpressionDecision::kAllowed);
  }

  // Keep the legacy input alias while exposing only real web faces.
  assert(sesame::robot::validate_expression("default", 1'000) ==
         sesame::robot::ExpressionDecision::kAllowed);

  // Pointing and bowing are still valid face names, but they must no longer
  // be executable robot actions.
  assert(!sesame::robot::is_web_action("point"));
  assert(!sesame::robot::is_web_action("bow"));
  assert(sesame::robot::validate_action(
             {"req_removed", "point", 1'000, kNowMs}, kNowMs, true) ==
         sesame::robot::ActionDecision::kUnknownAction);
  assert(sesame::robot::validate_action(
             {"req_removed", "bow", 1'000, kNowMs}, kNowMs, true) ==
         sesame::robot::ActionDecision::kUnknownAction);
  assert(sesame::robot::is_web_expression("point"));
  assert(sesame::robot::is_web_expression("bow"));
}
