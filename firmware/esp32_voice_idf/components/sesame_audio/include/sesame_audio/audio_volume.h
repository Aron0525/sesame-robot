#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace sesame::audio {

// Match ESP32-audioI2S's default setVolume() range and its logarithmic curve.
inline constexpr uint8_t kSpeakerVolumeSteps = 21;
inline constexpr uint8_t kSpeakerVolumeLevel = 15;

inline float speaker_volume_gain() {
  static const float gain = []() {
    constexpr float normalized =
        static_cast<float>(kSpeakerVolumeLevel) / kSpeakerVolumeSteps;
    constexpr float decibels = -112.0f * normalized * normalized * normalized +
                               172.0f * normalized * normalized - 60.0f;
    return std::pow(10.0f, decibels / 20.0f);
  }();
  return gain;
}

inline int16_t scale_speaker_pcm16(int16_t sample) {
  const float scaled = static_cast<float>(sample) * speaker_volume_gain();
  if (scaled >= std::numeric_limits<int16_t>::max()) {
    return std::numeric_limits<int16_t>::max();
  }
  if (scaled <= std::numeric_limits<int16_t>::min()) {
    return std::numeric_limits<int16_t>::min();
  }
  return static_cast<int16_t>(std::lround(scaled));
}

}  // namespace sesame::audio
