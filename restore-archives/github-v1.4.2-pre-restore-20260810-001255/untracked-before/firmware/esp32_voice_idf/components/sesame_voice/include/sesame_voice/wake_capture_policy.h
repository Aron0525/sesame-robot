#pragma once

namespace sesame::voice {

// WakeNet needs a continuous microphone feed. Gateway readiness controls
// whether a detected wake may start a cloud turn, not whether audio reaches
// the local wake-word engine.
constexpr bool should_capture_for_wake(bool tts_active) { return !tts_active; }

}  // namespace sesame::voice
