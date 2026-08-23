#pragma once

#include "sesame_voice/voice_turn_detector.h"

namespace sesame::voice {

// 200 ms of consecutive VAD speech rejects short room-noise impulses while
// the controller's pre-roll preserves the first syllables for ASR.
constexpr VoiceTurnDetectorConfig production_voice_turn_config() {
  return {
      .first_speech_timeout_ms = 3000,
      .endpoint_silence_ms = 1000,
      .maximum_listen_ms = 10000,
      .speech_start_frames = 10,
      .speech_continue_frames = 2,
  };
}

}  // namespace sesame::voice
