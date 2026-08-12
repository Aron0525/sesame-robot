#include "sesame_voice/recording_button.h"

namespace sesame::voice {

ButtonEvent RecordingButton::update(bool pressed, uint64_t now_ms) {
  if (!initialized_) {
    initialized_ = true;
    raw_pressed_ = pressed;
    stable_pressed_ = pressed;
    raw_changed_ms_ = now_ms;
    // A low GPIO0 during USB reset is synchronization, not a user press.
    // Starting released is immediately safe; starting pressed must first
    // complete a debounced release before a press can be accepted.
    release_armed_ = !pressed;
    return ButtonEvent::kNone;
  }

  if (pressed != raw_pressed_) {
    raw_pressed_ = pressed;
    raw_changed_ms_ = now_ms;
  }

  ButtonEvent event = ButtonEvent::kNone;
  if (raw_pressed_ != stable_pressed_ &&
      now_ms - raw_changed_ms_ >= debounce_ms_) {
    stable_pressed_ = raw_pressed_;
    if (!stable_pressed_) {
      release_armed_ = true;
    } else if (release_armed_) {
      // Consume the arm on the press edge. Holding the key cannot retrigger;
      // another event requires a separately debounced release and press.
      release_armed_ = false;
      event = ButtonEvent::kPressed;
    }
  }

  return event;
}

void RecordingButton::reset() {
  initialized_ = false;
  release_armed_ = false;
  raw_pressed_ = false;
  stable_pressed_ = false;
  raw_changed_ms_ = 0;
}

}  // namespace sesame::voice
