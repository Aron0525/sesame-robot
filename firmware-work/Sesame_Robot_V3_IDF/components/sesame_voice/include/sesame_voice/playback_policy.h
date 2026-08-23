#pragma once

#include <cstddef>

namespace sesame::voice {

// At 20 ms per Opus packet this provides 1.2 s of jitter capacity and waits
// for 600 ms before playback. These are the values proven by the standalone
// speaker test and prevent a delayed WSS acknowledgement from causing gaps.
inline constexpr size_t kPlaybackQueueCapacity = 60;
inline constexpr size_t kPlaybackStartupFrames = 30;
inline constexpr size_t kPlaybackLowWaterFrames = 20;
inline constexpr size_t kPlaybackHighWaterFrames = 40;
inline constexpr uint32_t kPlaybackStatsIntervalFrames = 5;

constexpr bool can_start_playback(size_t queued_frames, bool stream_ended) {
  return queued_frames >= kPlaybackStartupFrames ||
         (stream_ended && queued_frames > 0);
}

constexpr bool should_report_playback_stats(uint32_t rendered_frames,
                                            bool state_changed) {
  return state_changed ||
         (rendered_frames > 0 &&
          rendered_frames % kPlaybackStatsIntervalFrames == 0);
}

}  // namespace sesame::voice
