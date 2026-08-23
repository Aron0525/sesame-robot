#include <cassert>

#include "sesame_voice/playback_control.h"

int main() {
  sesame::voice::PlaybackControl control;
  assert(!control.set_paused(7, true));
  control.begin(7);
  assert(control.set_paused(7, true));
  assert(control.paused());
  assert(!control.set_paused(8, false));
  assert(control.paused());
  assert(control.set_paused(7, false));
  assert(!control.paused());
  control.clear();
  assert(control.generation_id() == 0);
  assert(!control.paused());
  return 0;
}
