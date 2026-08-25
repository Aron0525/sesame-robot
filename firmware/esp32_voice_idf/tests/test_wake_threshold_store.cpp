#include <cassert>

#include "sesame_voice/wake_capture_policy.h"

int main() {
  assert(sesame::voice::is_valid_wake_threshold_hundredths(5));
  assert(sesame::voice::is_valid_wake_threshold_hundredths(95));
  assert(!sesame::voice::is_valid_wake_threshold_hundredths(4));
  assert(!sesame::voice::is_valid_wake_threshold_hundredths(96));
}
