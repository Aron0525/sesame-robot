#pragma once

#include <cstddef>

namespace sesame::voice {

// At 20 ms per Opus packet, this keeps 1.2 seconds of jitter capacity and
// starts normal TTS only after 600 ms is ready. A completed short sentence
// may start with whatever audio it contains.
inline constexpr size_t kPlaybackQueueCapacity = 60;
inline constexpr size_t kPlaybackStartupFrames = 30;

constexpr bool can_start_playback(size_t queued_frames, bool stream_ended) {
  return queued_frames >= kPlaybackStartupFrames ||
         (stream_ended && queued_frames > 0);
}

}  // namespace sesame::voice
