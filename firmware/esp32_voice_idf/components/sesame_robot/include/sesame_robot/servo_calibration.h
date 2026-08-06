#pragma once

#include <cstdint>

namespace sesame::robot {

constexpr uint8_t kMaximumServoAngle = 180;
constexpr uint8_t kR3ServoIndex = 5;
constexpr uint8_t kL4ServoIndex = 7;
constexpr uint8_t kR3L4MaximumServoAngle = 150;

constexpr bool is_r3_or_l4(uint8_t servo_index) {
  return servo_index == kR3ServoIndex || servo_index == kL4ServoIndex;
}

// R3 and L4 use their authored direction, but their mechanical range ends at
// 150 degrees. All other channels retain the normal 0..180 degree range.
constexpr uint8_t physical_angle_for_servo(uint8_t servo_index,
                                           uint8_t logical_angle) {
  if (is_r3_or_l4(servo_index) && logical_angle > kR3L4MaximumServoAngle) {
    return kR3L4MaximumServoAngle;
  }
  return logical_angle > kMaximumServoAngle ? kMaximumServoAngle
                                             : logical_angle;
}

}  // namespace sesame::robot
