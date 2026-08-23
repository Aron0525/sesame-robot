#include <cstdint>
#include <iostream>
#include "sesame_web/legacy_motion_calibration.h"
int main() {
  const uint8_t inputs[8] = {90, 90, 90, 90, 0, 90, 0, 180};
  for (uint8_t channel = 0; channel < 8; ++channel) {
    if (channel) std::cout << ',';
    std::cout << static_cast<int>(sesame::web::physical_angle_for_legacy_motion(channel, inputs[channel]));
  }
  std::cout << '\n';
}
