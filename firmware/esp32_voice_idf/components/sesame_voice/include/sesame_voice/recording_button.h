#pragma once

#include <cstdint>

namespace sesame::voice {

enum class ButtonEvent {
  kNone = 0,
  kPressed,
};

class RecordingButton {
 public:
  explicit RecordingButton(uint32_t debounce_ms) : debounce_ms_(debounce_ms) {}

  ButtonEvent update(bool pressed, uint64_t now_ms);
  void reset();

 private:
  uint32_t debounce_ms_;
  bool initialized_{false};
  bool release_armed_{false};
  bool raw_pressed_{false};
  bool stable_pressed_{false};
  uint64_t raw_changed_ms_{0};
};

}  // namespace sesame::voice
