#include <cassert>

#include "sesame_audio/audio_contract.h"

int main() {
  using sesame::audio::kSpeakerDmaDescriptorCount;
  using sesame::audio::kSpeakerDmaFramesPerDescriptor;
  using sesame::audio::kSpeakerDmaAutoClear;

  // These values reproduce the known-good MAX98357A Arduino configuration:
  // 8 DMA descriptors, 128 stereo frames each, cleared after transmission.
  static_assert(kSpeakerDmaDescriptorCount == 8);
  static_assert(kSpeakerDmaFramesPerDescriptor == 128);
  static_assert(kSpeakerDmaAutoClear);

  assert(kSpeakerDmaDescriptorCount == 8);
  assert(kSpeakerDmaFramesPerDescriptor == 128);
  assert(kSpeakerDmaAutoClear);
}
