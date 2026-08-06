#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_robot/servo_calibration.h"

int main() {
  using sesame::robot::kL4ServoIndex;
  using sesame::robot::kMaximumServoAngle;
  using sesame::robot::kR3L4MaximumServoAngle;
  using sesame::robot::kR3ServoIndex;
  using sesame::robot::physical_angle_for_servo;

  constexpr std::array<int, 5> kNormalAngles{0, 67, 90, 150, 180};
  for (uint8_t servo_index = 0; servo_index < 8; ++servo_index) {
    for (const int logical_angle : kNormalAngles) {
      const auto logical = static_cast<uint8_t>(logical_angle);
      const auto expected =
          (servo_index == kR3ServoIndex || servo_index == kL4ServoIndex) &&
                  logical > kR3L4MaximumServoAngle
              ? kR3L4MaximumServoAngle
              : logical;
      assert(physical_angle_for_servo(servo_index, logical) == expected);
    }
  }

  assert(physical_angle_for_servo(kR3ServoIndex, 67) == 67);
  assert(physical_angle_for_servo(kL4ServoIndex, 150) == 150);
  assert(physical_angle_for_servo(kR3ServoIndex, 151) == 150);
  assert(physical_angle_for_servo(kL4ServoIndex, 180) == 150);
  assert(physical_angle_for_servo(4, 180) == 180);
  assert(physical_angle_for_servo(0, 255) == kMaximumServoAngle);
  assert(physical_angle_for_servo(kR3ServoIndex, 255) == 150);
}
