#include "sesame_voice/recording_button.h"

namespace sesame::voice {

ButtonEvent RecordingButton::update(bool pressed, uint64_t now_ms) {
  if (pressed != raw_pressed_) {
    raw_pressed_ = pressed;
    raw_changed_ms_ = now_ms;
  }

  ButtonEvent event = ButtonEvent::kNone;
  if (raw_pressed_ != stable_pressed_ &&
      now_ms - raw_changed_ms_ >= debounce_ms_) {
    stable_pressed_ = raw_pressed_;
    if (stable_pressed_) {
      recording_ = !recording_;
      if (recording_) {
        recording_started_ms_ = now_ms;
        event = ButtonEvent::kStartRecording;
      } else {
        event = ButtonEvent::kStopRecording;
      }
    }
  }

  if (recording_ &&
      now_ms - recording_started_ms_ >= maximum_recording_ms_) {
    recording_ = false;
    event = ButtonEvent::kMaximumDuration;
  }
  return event;
}

void RecordingButton::reset() {
  raw_pressed_ = false;
  stable_pressed_ = false;
  recording_ = false;
  raw_changed_ms_ = 0;
  recording_started_ms_ = 0;
}

}  // namespace sesame::voice
