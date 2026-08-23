#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_robot/servo_calibration.h"

int main() {
  using sesame::robot::kMaximumServoAngle;
  using sesame::robot::physical_angle_for_servo;

  constexpr std::array<int, 5> kLogicalAngles{0, 45, 90, 135, 180};
  for (uint8_t servo_index = 0; servo_index < 8; ++servo_index) {
    for (const int logical_angle : kLogicalAngles) {
      const auto logical = static_cast<uint8_t>(logical_angle);
      assert(physical_angle_for_servo(servo_index, logical) == logical);
    }
  }

  assert(physical_angle_for_servo(5, 0) == 0);
  assert(physical_angle_for_servo(5, 180) == 180);
  assert(physical_angle_for_servo(7, 0) == 0);
  assert(physical_angle_for_servo(7, 180) == 180);
  assert(physical_angle_for_servo(0, 255) == kMaximumServoAngle);
}
