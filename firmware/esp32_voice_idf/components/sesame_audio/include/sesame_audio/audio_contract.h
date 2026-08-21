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

// I2S0 RX: INMP441. This bus is intentionally independent from the speaker.
inline constexpr int kMicrophoneBclkGpio = 14;
inline constexpr int kMicrophoneWsGpio = 47;
inline constexpr int kMicrophoneDataGpio = 48;

// I2S1 TX: M5Stack Hat SPK2. The Hat has no host-controlled SD/EN signal.
inline constexpr int kSpeakerBclkGpio = 1;
inline constexpr int kSpeakerWsGpio = 2;
inline constexpr int kSpeakerDataGpio = 3;
// Match the working MAX98357A I2S configuration validated on this board.
inline constexpr uint32_t kSpeakerDmaDescriptorCount = 8;
inline constexpr uint32_t kSpeakerDmaFramesPerDescriptor = 128;
inline constexpr bool kSpeakerDmaAutoClear = true;
inline constexpr int kVoiceButtonGpio = 0;

static_assert(kSamplesPerFrame == 320);
static_assert(kPcmBytesPerFrame == 640);

}  // namespace sesame::audio
