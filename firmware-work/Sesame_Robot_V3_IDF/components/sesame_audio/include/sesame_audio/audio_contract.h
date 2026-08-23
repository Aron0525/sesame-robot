#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::audio {

inline constexpr uint32_t kSampleRateHz = 16000;
inline constexpr uint32_t kFrameDurationMs = 20;
inline constexpr size_t kSamplesPerFrame =
    kSampleRateHz * kFrameDurationMs / 1000;
inline constexpr size_t kPcmBytesPerFrame =
    kSamplesPerFrame * sizeof(int16_t);
inline constexpr size_t kI2sSlotsPerFrame = 2;
inline constexpr size_t kRawI2sSamplesPerFrame =
    kSamplesPerFrame * kI2sSlotsPerFrame;

// The microphone stays on the original I2S bus. The replacement MAX98357A
// speaker module uses the independently verified GPIO 1/2/3 wiring, so it
// must use the second ESP32-S3 I2S controller rather than share microphone
// clocks.
inline constexpr int kMicrophoneI2sPort = 0;
inline constexpr int kSpeakerI2sPort = 1;
inline constexpr int kMicrophoneBclkGpio = 14;
inline constexpr int kMicrophoneWsGpio = 47;
inline constexpr int kMicrophoneDataGpio = 48;
inline constexpr int kSpeakerBclkGpio = 1;
inline constexpr int kSpeakerWsGpio = 2;
inline constexpr int kSpeakerDataGpio = 3;
inline constexpr int kVoiceButtonGpio = 0;
inline constexpr int32_t kSpeakerGainQ15 = 16384;  // 0.5, proven MAX98357A headroom.

constexpr int32_t speaker_sample_to_i2s32(int16_t sample) {
  const int64_t scaled =
      (static_cast<int64_t>(sample) * kSpeakerGainQ15) >> 15;
  // Multiplication is defined for negative samples; left-shifting a negative
  // signed value is undefined behavior in C++.
  return static_cast<int32_t>(scaled * 65536LL);
}

static_assert(kSamplesPerFrame == 320);
static_assert(kPcmBytesPerFrame == 640);

}  // namespace sesame::audio
