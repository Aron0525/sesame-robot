#include <cstdint>
#include <iostream>
#include "sesame_robot/servo_calibration.h"
int main() {
  using namespace sesame::robot;
  for (uint8_t input : {uint8_t{0}, uint8_t{45}, uint8_t{90}, uint8_t{135}, uint8_t{180}}) {
    std::cout << "input=" << unsigned(input)
              << " R3(S5/GPIO11)=" << unsigned(physical_angle_for_servo(kR3ServoIndex, input))
              << " L4(S7/GPIO13)=" << unsigned(physical_angle_for_servo(kL4ServoIndex, input))
              << "\n";
  }
}
