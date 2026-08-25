#include "sesame_web/web_command.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace sesame::web {
namespace {

bool is_legacy_action(std::string_view action) {
  return std::find(kLegacyActions.begin(), kLegacyActions.end(), action) !=
         kLegacyActions.end();
}

template <size_t N>
void copy_string(std::array<char, N>* destination, std::string_view value) {
  const size_t size = std::min(value.size(), destination->size() - 1);
  std::memcpy(destination->data(), value.data(), size);
  (*destination)[size] = '\0';
}

}  // namespace

WebCommand parse_web_command(std::string_view action,
                             std::string_view expression,
                             int servo_number, int servo_angle) {
  WebCommand command{};

  if (servo_number != -1 || servo_angle != -1) {
    if (servo_number < 1 || servo_number > 8 || servo_angle < 0 ||
        servo_angle > 180) {
      return command;
    }
    command.kind = WebCommandKind::kManualServo;
    command.servo_index = static_cast<int8_t>(servo_number - 1);
    command.servo_angle = static_cast<uint8_t>(servo_angle);
    return command;
  }

  if (action == "stop") {
    command.kind = WebCommandKind::kStop;
    return command;
  }

  if (!action.empty()) {
    if (!is_legacy_action(action)) return command;
    command.kind = WebCommandKind::kLegacyAction;
    copy_string(&command.action, action);
    return command;
  }

  if (!expression.empty()) {
    command.kind = WebCommandKind::kFaceOnly;
    copy_string(&command.expression, expression);
  }
  return command;
}

}  // namespace sesame::web
