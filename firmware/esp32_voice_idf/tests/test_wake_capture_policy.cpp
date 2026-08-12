#include <cassert>

#include "sesame_voice/wake_capture_policy.h"

int main() {
  assert(sesame::voice::should_capture_for_wake(false));
  assert(sesame::voice::should_capture_for_wake(true));
}
