#pragma once

#include <cstdint>

namespace sesame::voice {

enum class ButtonEvent {
  kNone = 0,
  kStartRecording,
  kStopRecording,
  kMaximumDuration,
};

class RecordingButton {
 public:
  RecordingButton(uint32_t debounce_ms, uint32_t maximum_recording_ms)
      : debounce_ms_(debounce_ms),
        maximum_recording_ms_(maximum_recording_ms) {}

  ButtonEvent update(bool pressed, uint64_t now_ms);
  void reset();
  bool recording() const { return recording_; }

 private:
  uint32_t debounce_ms_;
  uint32_t maximum_recording_ms_;
  bool raw_pressed_{false};
  bool stable_pressed_{false};
  bool recording_{false};
  uint64_t raw_changed_ms_{0};
  uint64_t recording_started_ms_{0};
};

}  // namespace sesame::voice
