#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "sesame_robot/control_catalog.h"

namespace sesame::web {

inline constexpr const auto& kLegacyActions = sesame::robot::kWebActions;

enum class WebCommandKind : uint8_t {
  kRejected,
  kStop,
  kLegacyAction,
  kFaceOnly,
  kManualServo,
};

struct WebCommand {
  WebCommandKind kind{WebCommandKind::kRejected};
  std::array<char, 20> action{};
  std::array<char, 24> expression{};
  int8_t servo_index{-1};
  uint8_t servo_angle{0};

  [[nodiscard]] bool accepted() const {
    return kind != WebCommandKind::kRejected;
  }
};

WebCommand parse_web_command(std::string_view action,
                             std::string_view expression,
                             int servo_number, int servo_angle);

}  // namespace sesame::web

