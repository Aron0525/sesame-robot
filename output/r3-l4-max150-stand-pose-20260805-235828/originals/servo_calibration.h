#pragma once

#include <cstdint>

namespace sesame::robot {

constexpr uint8_t kMaximumServoAngle = 180;

// Servo commands use their authored angle directly. R3 and L4 are no longer
// electrically inverted at the PWM output layer.
constexpr uint8_t physical_angle_for_servo(uint8_t servo_index,
                                           uint8_t logical_angle) {
  static_cast<void>(servo_index);
  return logical_angle > kMaximumServoAngle ? kMaximumServoAngle
                                             : logical_angle;
}

}  // namespace sesame::robot
