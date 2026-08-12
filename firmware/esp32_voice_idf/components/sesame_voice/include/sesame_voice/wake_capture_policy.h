#pragma once

namespace sesame::voice {

// Continuous conversation permits the explicit wake phrase to interrupt TTS.
// During playback the turn detector ignores raw VAD and accepts only a full
// MultiNet command match, limiting speaker-echo sensitivity without AEC.
constexpr bool should_capture_for_wake(bool) { return true; }

}  // namespace sesame::voice
