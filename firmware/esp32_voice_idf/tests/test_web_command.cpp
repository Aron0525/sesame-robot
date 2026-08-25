#include <cassert>

#include "sesame_web/web_command.h"

int main() {
  using sesame::web::WebCommandKind;
  using sesame::web::parse_web_command;

  const auto wave = parse_web_command("wave", "", -1, -1);
  assert(wave.kind == WebCommandKind::kLegacyAction);
  assert(wave.accepted());

  const auto movement = parse_web_command("forward", "", -1, -1);
  assert(movement.kind == WebCommandKind::kLegacyAction);
  assert(movement.accepted());

  const auto face = parse_web_command("", "happy", -1, -1);
  assert(face.kind == WebCommandKind::kFaceOnly);
  assert(face.accepted());

  const auto servo = parse_web_command("", "", 3, 90);
  assert(servo.kind == WebCommandKind::kManualServo);
  assert(servo.servo_index == 2);
  assert(servo.servo_angle == 90);

  const auto rejected_servo = parse_web_command("", "", 9, 181);
  assert(!rejected_servo.accepted());

  const auto rejected_action = parse_web_command("erase_flash", "", -1, -1);
  assert(!rejected_action.accepted());
}
