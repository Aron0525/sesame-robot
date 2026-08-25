#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace sesame::robot {

struct MotionStep {
  std::array<uint8_t, 8> angles;
  uint16_t dwell_ms;
};

std::span<const MotionStep> motion_plan_for(std::string_view action);

}  // namespace sesame::robot
