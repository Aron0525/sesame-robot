#pragma once

#include <cstdint>
#include <string_view>

namespace sesame::robot {

constexpr uint8_t kMaximumServoAngle = 180;
constexpr uint8_t kR3ServoIndex = 5;
constexpr uint8_t kL4ServoIndex = 7;
constexpr uint8_t kR3L4MaximumServoAngle = 150;

constexpr bool is_r3_or_l4(uint8_t servo_index) {
  return servo_index == kR3ServoIndex || servo_index == kL4ServoIndex;
}

// This is intentionally restricted to the forward gait test. It lets the
// operator distinguish a 150-degree output cap from a per-channel motion
// problem without changing any other action or manual-servo command.
constexpr bool preserves_r3_l4_full_range_for_action(std::string_view action) {
  return action == "forward";
}

// R3 and L4 use their authored direction, but their mechanical range ends at
// 150 degrees. The forward gait diagnostic may bypass that cap only for those
// two channels. All other channels retain the normal 0..180 degree range.
constexpr uint8_t physical_angle_for_servo(uint8_t servo_index,
                                           uint8_t logical_angle,
                                           bool preserve_r3_l4_full_range = false) {
  const uint8_t bounded_angle = logical_angle > kMaximumServoAngle
                                    ? kMaximumServoAngle
                                    : logical_angle;
  if (!preserve_r3_l4_full_range && is_r3_or_l4(servo_index) &&
      bounded_angle > kR3L4MaximumServoAngle) {
    return kR3L4MaximumServoAngle;
  }
  return bounded_angle;
}

}  // namespace sesame::robot

