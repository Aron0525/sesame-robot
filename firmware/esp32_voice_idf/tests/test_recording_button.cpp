#include <cassert>
#include <cstdint>

#include "sesame_voice/recording_button.h"

using sesame::voice::ButtonEvent;
using sesame::voice::RecordingButton;

int main() {
  RecordingButton button(80);

  // GPIO0 may be low while USB resets the board. A held-low startup must not
  // become a synthetic press; a stable release arms the physical button.
  assert(button.update(true, 0) == ButtonEvent::kNone);
  assert(button.update(true, 500) == ButtonEvent::kNone);
  assert(button.update(false, 501) == ButtonEvent::kNone);
  assert(button.update(false, 580) == ButtonEvent::kNone);
  assert(button.update(false, 581) == ButtonEvent::kNone);

  // Press bounce: only 80 ms after the final transition is accepted.
  assert(button.update(true, 600) == ButtonEvent::kNone);
  assert(button.update(false, 620) == ButtonEvent::kNone);
  assert(button.update(true, 640) == ButtonEvent::kNone);
  assert(button.update(true, 719) == ButtonEvent::kNone);
  assert(button.update(true, 720) == ButtonEvent::kPressed);

  // Holding the key produces no repeated event.
  assert(button.update(true, 1200) == ButtonEvent::kNone);

  // Release bounce produces no action, but a stable release rearms the key.
  assert(button.update(false, 1300) == ButtonEvent::kNone);
  assert(button.update(true, 1320) == ButtonEvent::kNone);
  assert(button.update(false, 1340) == ButtonEvent::kNone);
  assert(button.update(false, 1419) == ButtonEvent::kNone);
  assert(button.update(false, 1420) == ButtonEvent::kNone);

  // The next independently debounced press emits exactly one more edge. The
  // controller, not this debouncer, decides whether that means start or stop.
  assert(button.update(true, 1500) == ButtonEvent::kNone);
  assert(button.update(true, 1579) == ButtonEvent::kNone);
  assert(button.update(true, 1580) == ButtonEvent::kPressed);
  assert(button.update(true, 2000) == ButtonEvent::kNone);

  return 0;
}
