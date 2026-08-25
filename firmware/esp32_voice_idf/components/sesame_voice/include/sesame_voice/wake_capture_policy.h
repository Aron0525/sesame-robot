#pragma once

#include <cstdint>

namespace sesame::voice {

// There is no AEC reference channel. Playback is therefore strict half-duplex:
// microphone frames must not enter VAD or MultiNet while the speaker is on.
constexpr bool should_capture_for_wake(bool tts_active) { return !tts_active; }

constexpr bool is_valid_wake_threshold_hundredths(uint8_t value) {
  return value >= 5 && value <= 95;
}

}  // namespace sesame::voice
