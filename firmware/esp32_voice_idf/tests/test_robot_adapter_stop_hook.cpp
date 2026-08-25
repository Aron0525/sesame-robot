#include <cassert>
#include <string_view>
#include <vector>

#include "sesame_robot/robot_adapter.h"

namespace {

class FakeRobotDriver final : public sesame::robot::RobotDriver {
 public:
  bool safe_for_motion() const override { return safe; }
  bool execute_action(const char*, uint32_t) override {
    ++execute_action_calls;
    return true;
  }
  bool set_expression(const char* expression, uint32_t) override {
    last_expression = expression;
    return true;
  }
  bool begin_manual_control() override {
    events->push_back("manual");
    return true;
  }
  bool set_manual_angle(uint8_t servo_index, uint8_t angle) override {
    last_servo_index = servo_index;
    last_angle = angle;
    return true;
  }
  void emergency_stop() override { events->push_back("driver"); }

  std::vector<const char*>* events{nullptr};
  bool safe{true};
  int execute_action_calls{0};
  const char* last_expression{nullptr};
  uint8_t last_servo_index{255};
  uint8_t last_angle{255};
};

void cancel_web_motion(void* context) {
  static_cast<std::vector<const char*>*>(context)->push_back("web");
}

struct MotionExecution {
  const char* action;
  bool show_action_face;
};

std::vector<MotionExecution>* g_motion_executions = nullptr;

bool start_legacy_motion(void* context, const char* action,
                         bool show_action_face) {
  static_cast<std::vector<const char*>*>(context)->push_back(action);
  g_motion_executions->push_back({action, show_action_face});
  return true;
}

bool set_legacy_motion_settings(void* context, int frame_delay_ms,
                                int walk_cycles, int motor_current_delay_ms) {
  if (frame_delay_ms != 100 || walk_cycles != 10 ||
      motor_current_delay_ms != 20) {
    return false;
  }
  static_cast<std::vector<const char*>*>(context)->push_back("settings");
  return true;
}

}  // namespace

int main() {
  std::vector<const char*> events;
  std::vector<MotionExecution> motion_executions;
  g_motion_executions = &motion_executions;
  FakeRobotDriver driver;
  driver.events = &events;

  sesame::robot::RobotAdapter adapter(&driver, cancel_web_motion, &events);
  adapter.emergency_stop();

  assert((events == std::vector<const char*>{"web", "driver"}));

  events.clear();
  adapter.set_action_executor(start_legacy_motion, &events);
  assert(adapter.execute_operator_action("dance"));
  assert(!adapter.execute_operator_action("not-a-real-action"));
  assert((events == std::vector<const char*>{"dance"}));
  assert(motion_executions.size() == 1);
  assert(std::string_view(motion_executions.back().action) == "dance");
  assert(motion_executions.back().show_action_face);

  // Response-plan actions must use the same legacy motion runner as the web
  // page, but keep the model-selected expression instead of replacing it with
  // the action's built-in face.
  const sesame::robot::ActionRequest stand_request{
      "req_stand", "stand", 1000, 1000};
  assert(adapter.execute(stand_request, 0) ==
         sesame::robot::ActionDecision::kAllowed);
  assert(motion_executions.size() == 2);
  assert(std::string_view(motion_executions.back().action) == "stand");
  assert(!motion_executions.back().show_action_face);
  assert(driver.execute_action_calls == 0);

  const sesame::robot::ActionRequest dance_request{
      "req_dance", "dance", 1000, 1000};
  assert(adapter.execute(dance_request, 0) ==
         sesame::robot::ActionDecision::kAllowed);
  assert(motion_executions.size() == 3);
  assert(std::string_view(motion_executions.back().action) == "dance");
  assert(!motion_executions.back().show_action_face);

  // A WSS disconnect issues emergency_stop(), which leaves safe_for_motion()
  // false until the legacy executor calls begin_web_motion(). The next
  // authenticated operator action must reach that executor so it can re-arm.
  driver.safe = false;
  assert(adapter.execute_operator_action("wave"));
  assert((events == std::vector<const char*>{"dance", "stand", "dance", "wave"}));

  // The operator console exposes the complete OLED face catalog. It must not
  // be restricted relative to the model-generated expression allowlist.
  assert(adapter.set_operator_expression("cute"));
  assert(std::string_view(driver.last_expression) == "cute");
  assert(adapter.set_expression("talk_happy", 10000) ==
         sesame::robot::ExpressionDecision::kAllowed);

  assert(adapter.set_manual_servo(2, 135));
  assert(driver.last_servo_index == 1);
  assert(driver.last_angle == 135);
  assert((events ==
          std::vector<const char*>{"dance", "stand", "dance", "wave", "web", "manual"}));

  adapter.set_motion_settings_executor(set_legacy_motion_settings, &events);
  assert(adapter.configure_motion(100, 10, 20));
  assert((events == std::vector<const char*>{"dance", "stand", "dance", "wave",
                                             "web", "manual", "settings"}));
}
