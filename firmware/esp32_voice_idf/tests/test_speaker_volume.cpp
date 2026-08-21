#include <cassert>
#include <cstdint>

#include "sesame_audio/audio_volume.h"

int main() {
  using sesame::audio::kSpeakerVolumeLevel;
  using sesame::audio::kSpeakerVolumeSteps;
  using sesame::audio::scale_speaker_pcm16;

  static_assert(kSpeakerVolumeSteps == 21);
  static_assert(kSpeakerVolumeLevel == 15);
  assert(scale_speaker_pcm16(0) == 0);

  // Level 15 applies a noticeable but non-silent attenuation on the
  // reference library's 0…21 logarithmic curve.
  const int16_t positive = scale_speaker_pcm16(32767);
  const int16_t negative = scale_speaker_pcm16(-32768);
  assert(positive > 0 && positive < 32767);
  assert(negative < 0 && negative > -32768);
}
