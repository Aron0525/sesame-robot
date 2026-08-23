#include <cstdint>
#include <iostream>
#include "sesame_robot/servo_calibration.h"

int main() {
  using sesame::robot::kL4ServoIndex;
  using sesame::robot::kR3ServoIndex;
  using sesame::robot::physical_angle_for_servo;
  for (const auto input : {0, 90, 180}) {
    const auto angle = static_cast<uint8_t>(input);
    std::cout << "R3 " << input << " -> "
              << static_cast<int>(physical_angle_for_servo(kR3ServoIndex, angle))
              << '\n';
    std::cout << "L4 " << input << " -> "
              << static_cast<int>(physical_angle_for_servo(kL4ServoIndex, angle))
              << '\n';
  }
  std::cout << "R2 45 -> "
            << static_cast<int>(physical_angle_for_servo(1, 45)) << '\n';
}
