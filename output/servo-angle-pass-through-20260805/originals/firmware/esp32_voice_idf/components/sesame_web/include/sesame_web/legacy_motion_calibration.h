#pragma once

#include <cstdint>

namespace sesame::web {

// The legacy motion table uses the original mechanical zero for each joint.
// R4 and L3 use direct offsets. R3 and L4 are mirrored joints, so their
// physical command is 180 - theoretical_angle + 30, limited to 180 degrees.
constexpr uint8_t physical_angle_for_legacy_motion(uint8_t channel,
                                                   uint8_t theoretical_angle) {
  constexpr uint8_t kR4Channel = 4;
  constexpr uint8_t kR3Channel = 5;
  constexpr uint8_t kL3Channel = 6;
  constexpr uint8_t kL4Channel = 7;
  constexpr uint8_t kMaximumServoAngle = 180;

  uint16_t adjusted_angle = theoretical_angle;
  switch (channel) {
    case kR4Channel:
      adjusted_angle += 30;
      break;
    case kR3Channel:
      adjusted_angle = kMaximumServoAngle - theoretical_angle + 30;
      break;
    case kL3Channel:
      adjusted_angle += 10;
      break;
    case kL4Channel:
      adjusted_angle = kMaximumServoAngle - theoretical_angle + 30;
      break;
    default:
      break;
  }

  return static_cast<uint8_t>(adjusted_angle > kMaximumServoAngle
                                  ? kMaximumServoAngle
                                  : adjusted_angle);
}

}  // namespace sesame::web
