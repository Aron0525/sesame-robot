#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_audio/audio_buffers.h"
#include "sesame_audio/audio_contract.h"

int main() {
  using namespace sesame::audio;
  static_assert(kSampleRateHz == 16000);
  static_assert(kFrameDurationMs == 20);
  static_assert(kSamplesPerFrame == 320);
  static_assert(kPcmBytesPerFrame == 640);
  static_assert(kI2sSlotsPerFrame == 2);
  static_assert(kRawI2sSamplesPerFrame == 640);
  static_assert(kSpeakerDmaDescriptorFrames == 8);
  static_assert(kMicrophoneI2sPort == 0);
  static_assert(kSpeakerI2sPort == 1);
  static_assert(kMicrophoneBclkGpio == 14);
  static_assert(kMicrophoneWsGpio == 47);
  static_assert(kMicrophoneDataGpio == 48);
  static_assert(kSpeakerBclkGpio == 1);
  static_assert(kSpeakerWsGpio == 2);
  static_assert(kSpeakerDataGpio == 3);
  static_assert(kVoiceButtonGpio == 0);
  static_assert(speaker_sample_to_i2s32(32767) == 1073676288);
  static_assert(speaker_sample_to_i2s32(-32768) == -1073741824);
  static_assert(speaker_sample_to_i2s32(0) == 0);

  FixedRingBuffer<std::array<int16_t, kSamplesPerFrame>, 2> queue;
  std::array<int16_t, kSamplesPerFrame> frame{};
  assert(queue.push(frame));
  assert(queue.push(frame));
  assert(!queue.push(frame));
  assert(queue.full());
  assert(queue.pop(&frame));
  assert(queue.size() == 1);
  queue.clear();
  assert(queue.empty());
  return 0;
}
