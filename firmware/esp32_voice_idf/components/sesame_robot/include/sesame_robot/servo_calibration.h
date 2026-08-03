#pragma once

#include <cstdint>

namespace sesame::robot {

constexpr uint8_t physical_angle_for_servo(uint8_t servo_index,
                                           uint8_t logical_angle) {
  static_cast<void>(servo_index);
  constexpr uint8_t kMaximumServoAngle = 180;
  return logical_angle > kMaximumServoAngle ? kMaximumServoAngle
                                              : logical_angle;
}

}  // namespace sesame::robot
