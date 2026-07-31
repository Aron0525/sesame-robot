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

inline constexpr int kI2sBclkGpio = 14;
inline constexpr int kI2sWsGpio = 47;
inline constexpr int kMicrophoneDataGpio = 48;
inline constexpr int kSpeakerDataGpio = 2;
inline constexpr int kAmplifierEnableGpio = 1;
inline constexpr int kVoiceButtonGpio = 0;

static_assert(kSamplesPerFrame == 320);
static_assert(kPcmBytesPerFrame == 640);

}  // namespace sesame::audio
