#pragma once

#include <cstdint>

namespace sesame::robot {

constexpr uint8_t kMaximumServoAngle = 180;
constexpr uint8_t kR3ServoIndex = 5;
constexpr uint8_t kL4ServoIndex = 7;

constexpr uint8_t physical_angle_for_servo(uint8_t servo_index,
                                           uint8_t logical_angle) {
  if (logical_angle > kMaximumServoAngle) {
    return kMaximumServoAngle;
  }

  if (servo_index == kR3ServoIndex || servo_index == kL4ServoIndex) {
    return static_cast<uint8_t>(kMaximumServoAngle - logical_angle);
  }

  return logical_angle;
}

}  // namespace sesame::robot
