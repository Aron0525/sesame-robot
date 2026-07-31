#pragma once

#include <cstdint>

namespace sesame::robot {

// These are logical channels in the original Sesame motion tables. The two
// mirrored joints are a property of this robot's physical assembly; the
// upstream pose data remains in its original, logical coordinate system.
constexpr uint8_t kR3ServoIndex = 5;
constexpr uint8_t kL4ServoIndex = 7;

constexpr uint8_t physical_angle_for_servo(uint8_t servo_index,
                                           uint8_t logical_angle) {
  constexpr uint8_t kMaximumServoAngle = 180;
  const uint8_t bounded_angle = logical_angle > kMaximumServoAngle
                                    ? kMaximumServoAngle
                                    : logical_angle;
  if (servo_index == kR3ServoIndex || servo_index == kL4ServoIndex) {
    return static_cast<uint8_t>(kMaximumServoAngle - bounded_angle);
  }
  return bounded_angle;
}

}  // namespace sesame::robot
