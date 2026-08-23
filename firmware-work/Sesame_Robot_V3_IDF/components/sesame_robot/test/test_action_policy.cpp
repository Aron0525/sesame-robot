#include <cassert>

#include "sesame_robot/action_policy.h"

int main() {
  using namespace sesame::robot;

  ActionRequest wave{"req-1", "wave", 1200, 8000};
  assert(validate_action(wave, 7000, true) == ActionDecision::kAllowed);
  wave.action = "dance";
  assert(validate_action(wave, 7000, true) == ActionDecision::kAllowed);
  wave.action = "not-a-motion";
  assert(validate_action(wave, 7000, true) == ActionDecision::kUnknownAction);
  wave.action = "wave";
  wave.duration_ms = 12001;
  assert(validate_action(wave, 7000, true) ==
         ActionDecision::kDurationOutOfRange);
  wave.duration_ms = 1200;
  assert(validate_action(wave, 9000, true) == ActionDecision::kExpired);
  assert(validate_action(wave, 7000, false) == ActionDecision::kUnsafeState);

  assert(validate_expression("default", 100) ==
         ExpressionDecision::kAllowed);
  assert(validate_expression("happy", 10000) ==
         ExpressionDecision::kAllowed);
  assert(validate_expression("talk_happy", 1000) ==
         ExpressionDecision::kAllowed);
  assert(validate_expression("not-a-face", 1000) ==
         ExpressionDecision::kUnknownExpression);
  assert(validate_expression("happy", 10001) ==
         ExpressionDecision::kTtlOutOfRange);
}
