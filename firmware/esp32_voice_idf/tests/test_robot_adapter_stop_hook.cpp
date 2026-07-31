#include <cassert>
#include <vector>

#include "sesame_robot/robot_adapter.h"

namespace {

class FakeRobotDriver final : public sesame::robot::RobotDriver {
 public:
  bool safe_for_motion() const override { return true; }
  bool execute_action(const char*, uint32_t) override { return true; }
  bool set_expression(const char*, uint32_t) override { return true; }
  void emergency_stop() override { events->push_back("driver"); }

  std::vector<const char*>* events{nullptr};
};

void cancel_web_motion(void* context) {
  static_cast<std::vector<const char*>*>(context)->push_back("web");
}

}  // namespace

int main() {
  std::vector<const char*> events;
  FakeRobotDriver driver;
  driver.events = &events;

  sesame::robot::RobotAdapter adapter(&driver, cancel_web_motion, &events);
  adapter.emergency_stop();

  assert((events == std::vector<const char*>{"web", "driver"}));
}
