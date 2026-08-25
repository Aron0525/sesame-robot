#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "sesame_audio/audio_contract.h"

namespace sesame::voice {

// This is an enrolled-owner gate for a fixed spoken phrase, not a general or
// security-grade speaker-verification neural network. It intentionally fails
// closed when a private template has not been generated on the user's machine.
struct OwnerVoiceprintTemplate {
  static constexpr size_t kFeatureCount = 13;

  bool enabled{false};
  float threshold{1.0f};
  std::array<float, kFeatureCount> centroid{};
};

struct OwnerVoiceprintFeatures {
  bool valid{false};
  std::array<float, OwnerVoiceprintTemplate::kFeatureCount> values{};
};

struct OwnerVoiceprintDecision {
  bool template_available{false};
  bool has_sufficient_audio{false};
  bool accepted{false};
  float score{0.0f};
};

class OwnerVoiceGate final {
 public:
  static constexpr size_t kCaptureFrames =
      1500 / sesame::audio::kFrameDurationMs;

  explicit OwnerVoiceGate(OwnerVoiceprintTemplate template_data);

  void clear();
  void feed_pcm(const int16_t* pcm, size_t sample_count);
  OwnerVoiceprintFeatures extract_latest() const;
  OwnerVoiceprintDecision verify_latest() const;

 private:
  struct Frame {
    std::array<int16_t, sesame::audio::kSamplesPerFrame> samples{};
  };

  static constexpr size_t kMinimumVoicedFrames = 15;
  OwnerVoiceprintTemplate template_{};
  std::array<Frame, kCaptureFrames> frames_{};
  size_t oldest_{0};
  size_t size_{0};
};

static_assert(OwnerVoiceGate::kCaptureFrames == 75);

}  // namespace sesame::voice
