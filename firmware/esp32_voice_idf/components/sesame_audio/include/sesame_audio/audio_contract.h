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

// The microphone remains on its original I2S bus. The replacement MAX98357A
// uses independently wired GPIO 1/2/3, so it must use the second I2S port.
inline constexpr int kMicrophoneI2sPort = 0;
inline constexpr int kSpeakerI2sPort = 1;
inline constexpr int kMicrophoneBclkGpio = 14;
inline constexpr int kMicrophoneWsGpio = 47;
inline constexpr int kMicrophoneDataGpio = 48;
inline constexpr int kSpeakerBclkGpio = 1;
inline constexpr int kSpeakerWsGpio = 2;
inline constexpr int kSpeakerDataGpio = 3;
inline constexpr int kVoiceButtonGpio = 0;

static_assert(kSamplesPerFrame == 320);
static_assert(kPcmBytesPerFrame == 640);

}  // namespace sesame::audio
