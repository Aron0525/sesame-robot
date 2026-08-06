#include <cassert>
#include <vector>

#include "sesame_robot/robot_adapter.h"

namespace {

class FakeRobotDriver final : public sesame::robot::RobotDriver {
 public:
  bool safe_for_motion() const override { return safe; }
  bool execute_action(const char*, uint32_t) override { return true; }
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
  const char* last_expression{nullptr};
  uint8_t last_servo_index{255};
  uint8_t last_angle{255};
};

void cancel_web_motion(void* context) {
  static_cast<std::vector<const char*>*>(context)->push_back("web");
}

bool start_legacy_motion(void* context, const char* action) {
  static_cast<std::vector<const char*>*>(context)->push_back(action);
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

  // A WSS disconnect issues emergency_stop(), which leaves safe_for_motion()
  // false until the legacy executor calls begin_web_motion(). The next
  // authenticated operator action must reach that executor so it can re-arm.
  driver.safe = false;
  assert(adapter.execute_operator_action("wave"));
  assert((events == std::vector<const char*>{"dance", "wave"}));

  // The operator console exposes the complete OLED face catalog. It must not
  // be restricted to the smaller model-generated expression allowlist.
  assert(adapter.set_operator_expression("cute"));
  assert(std::string_view(driver.last_expression) == "cute");
  assert(adapter.set_expression("cute", 10000) ==
         sesame::robot::ExpressionDecision::kUnknownExpression);

  assert(adapter.set_manual_servo(2, 135));
  assert(driver.last_servo_index == 1);
  assert(driver.last_angle == 135);
  assert((events ==
          std::vector<const char*>{"dance", "wave", "web", "manual"}));

  adapter.set_motion_settings_executor(set_legacy_motion_settings, &events);
  assert(adapter.configure_motion(100, 10, 20));
  assert((events == std::vector<const char*>{"dance", "wave", "web",
                                             "manual", "settings"}));
}
