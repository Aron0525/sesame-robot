#include <cassert>

#include "sesame_audio/audio_contract.h"

int main() {
  using namespace sesame::audio;

  static_assert(kMicrophoneI2sPort == 0);
  static_assert(kSpeakerI2sPort == 1);
  static_assert(kMicrophoneBclkGpio == 14);
  static_assert(kMicrophoneWsGpio == 47);
  static_assert(kMicrophoneDataGpio == 48);
  static_assert(kSpeakerBclkGpio == 1);
  static_assert(kSpeakerWsGpio == 2);
  static_assert(kSpeakerDataGpio == 3);

  return 0;
}
