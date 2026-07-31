#pragma once

#include <cstdint>

#include "sesame_robot/action_policy.h"

namespace sesame::robot {

class RobotDriver {
 public:
  virtual ~RobotDriver() = default;
  virtual bool safe_for_motion() const = 0;
  virtual bool execute_action(const char* action, uint32_t duration_ms) = 0;
  virtual bool set_expression(const char* expression, uint32_t ttl_ms) = 0;
  virtual void emergency_stop() = 0;
};

class RobotAdapter {
 public:
  using EmergencyStopHook = void (*)(void* context);

  explicit RobotAdapter(RobotDriver* driver,
                        EmergencyStopHook before_driver_stop = nullptr,
                        void* hook_context = nullptr)
      : driver_(driver),
        before_driver_stop_(before_driver_stop),
        hook_context_(hook_context) {}

  ActionDecision execute(const ActionRequest& request, uint64_t now_ms);
  ExpressionDecision set_expression(const char* expression, uint32_t ttl_ms);
  void emergency_stop();

 private:
  RobotDriver* driver_;
  EmergencyStopHook before_driver_stop_;
  void* hook_context_;
};

}  // namespace sesame::robot
