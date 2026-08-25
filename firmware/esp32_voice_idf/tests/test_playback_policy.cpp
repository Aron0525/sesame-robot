#include <cassert>

#include "sesame_voice/playback_policy.h"

int main() {
  using namespace sesame::voice;

  static_assert(kPlaybackQueueCapacity == 60);
  static_assert(kPlaybackStartupFrames == 30);
  assert(!can_start_playback(kPlaybackStartupFrames - 1, false));
  assert(can_start_playback(kPlaybackStartupFrames, false));
  assert(can_start_playback(1, true));
  assert(!can_start_playback(0, true));
  return 0;
}
