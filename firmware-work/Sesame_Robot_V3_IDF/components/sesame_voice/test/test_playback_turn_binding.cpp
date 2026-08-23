#include <cassert>
#include <cstring>

#include "sesame_voice/playback_turn_binding.h"

int main() {
  sesame::voice::PlaybackTurnBinding binding;

  assert(!binding.bind(nullptr, 7));
  assert(!binding.bind("", 7));
  assert(!binding.bind("test_001", 0));
  assert(binding.bind("test_001", 7));
  assert(binding.generation_id() == 7);
  assert(std::strcmp(binding.turn_id(), "test_001") == 0);
  assert(binding.matches("test_001", 7));
  assert(!binding.matches("turn_old", 7));

  binding.clear();
  assert(binding.turn_id() == nullptr);
  assert(binding.generation_id() == 0);
  return 0;
}
