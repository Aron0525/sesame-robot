#include <cassert>

#include "sesame_voice/voice_turn_policy.h"

int main() {
  const auto config = sesame::voice::production_voice_turn_config();
  assert(config.first_speech_timeout_ms == 3000);
  assert(config.endpoint_silence_ms == 1000);
  assert(config.maximum_listen_ms == 10000);
  assert(config.speech_start_frames == 10);
  assert(config.speech_continue_frames == 2);
}
