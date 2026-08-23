#include <array>
#include <cassert>

#include "sesame_voice/playback_buffer.h"
#include "sesame_voice/playback_policy.h"

int main() {
  using namespace sesame::voice;

  static_assert(kPlaybackQueueCapacity == 60);
  static_assert(kPlaybackStartupFrames == 30);
  static_assert(kPlaybackLowWaterFrames == 20);
  static_assert(kPlaybackHighWaterFrames == 40);
  static_assert(kPlaybackStatsIntervalFrames == 5);
  assert(!can_start_playback(0, false));
  assert(!can_start_playback(kPlaybackStartupFrames - 1, false));
  assert(can_start_playback(kPlaybackStartupFrames, false));
  assert(can_start_playback(1, true));
  assert(!can_start_playback(0, true));
  assert(should_report_playback_stats(0, true));
  assert(!should_report_playback_stats(1, false));
  assert(!should_report_playback_stats(kPlaybackStatsIntervalFrames - 1,
                                       false));
  assert(should_report_playback_stats(kPlaybackStatsIntervalFrames, false));
  assert(should_report_playback_stats(1, true));

  PlaybackBuffer<2> buffer;
  PcmFrame frame{};
  frame.generation_id = 7;
  frame.sequence = 1;
  frame.samples.fill(12);

  buffer.begin_generation(7);
  assert(buffer.push(frame) == PlaybackPushResult::kAccepted);
  frame.sequence = 2;
  assert(buffer.push(frame) == PlaybackPushResult::kAccepted);
  frame.sequence = 3;
  assert(buffer.push(frame) == PlaybackPushResult::kFull);

  PcmFrame output{};
  assert(buffer.pop(&output));
  assert(output.sequence == 1);
  buffer.begin_generation(8);
  assert(buffer.size() == 0);
  frame.generation_id = 7;
  assert(buffer.push(frame) == PlaybackPushResult::kStaleGeneration);
}
