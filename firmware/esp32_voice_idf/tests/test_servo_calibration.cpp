#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_robot/servo_calibration.h"
#include "sesame_web/legacy_motion_calibration.h"

int main() {
  using sesame::robot::physical_angle_for_servo;

  constexpr std::array<int, 5> kLogicalAngles{0, 45, 90, 135, 180};
  for (uint8_t servo_index = 0; servo_index < 8; ++servo_index) {
    for (const int logical_angle : kLogicalAngles) {
      const auto logical = static_cast<uint8_t>(logical_angle);
      assert(physical_angle_for_servo(servo_index, logical) == logical);
    }
  }

  using sesame::web::physical_angle_for_legacy_motion;
  assert(physical_angle_for_legacy_motion(4, 0) == 30);   // R4
  assert(physical_angle_for_legacy_motion(5, 30) == 180); // R3
  assert(physical_angle_for_legacy_motion(6, 0) == 10);   // L3
  assert(physical_angle_for_legacy_motion(7, 30) == 180); // L4
  assert(physical_angle_for_legacy_motion(5, 90) == 120);
  assert(physical_angle_for_legacy_motion(7, 180) == 30);
  assert(physical_angle_for_legacy_motion(0, 90) == 90);
}
