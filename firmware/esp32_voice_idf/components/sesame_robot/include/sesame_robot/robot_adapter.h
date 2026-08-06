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
  virtual bool begin_manual_control() { return false; }
  virtual bool set_manual_angle(uint8_t, uint8_t) { return false; }
  virtual void emergency_stop() = 0;
};

class RobotAdapter {
 public:
  using EmergencyStopHook = void (*)(void* context);
  using ActionExecutor = bool (*)(void* context, const char* action);
  using MotionSettingsExecutor = bool (*)(void* context, int frame_delay_ms,
                                          int walk_cycles,
                                          int motor_current_delay_ms);

  explicit RobotAdapter(RobotDriver* driver,
                        EmergencyStopHook before_driver_stop = nullptr,
                        void* hook_context = nullptr)
      : driver_(driver),
        before_driver_stop_(before_driver_stop),
        hook_context_(hook_context) {}

  ActionDecision execute(const ActionRequest& request, uint64_t now_ms);
  bool execute_operator_action(const char* action);
  bool set_operator_expression(const char* expression);
  ExpressionDecision set_expression(const char* expression, uint32_t ttl_ms);
  bool set_manual_servo(uint8_t servo_number, uint8_t angle);
  void set_action_executor(ActionExecutor action_executor, void* context);
  void set_motion_settings_executor(MotionSettingsExecutor settings_executor,
                                    void* context);
  bool configure_motion(int frame_delay_ms, int walk_cycles,
                        int motor_current_delay_ms);
  void emergency_stop();

 private:
  RobotDriver* driver_;
  EmergencyStopHook before_driver_stop_;
  void* hook_context_;
  ActionExecutor action_executor_{nullptr};
  void* action_executor_context_{nullptr};
  MotionSettingsExecutor settings_executor_{nullptr};
  void* settings_executor_context_{nullptr};
};

}  // namespace sesame::robot
