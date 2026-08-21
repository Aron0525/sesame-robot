#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "sesame_audio/audio_contract.h"

namespace sesame::voice {

// Fixed-phrase, local speaker verification for ESP32-S3. This lightweight
// acoustic-template comparison reduces accidental activation but is not
// security-grade biometric authentication and cannot prevent replay attacks.
struct SpeakerVerificationTemplate {
  static constexpr size_t kFeatureCount = 13;

  bool available{false};
  float threshold{1.0f};
  std::array<float, kFeatureCount> centroid{};
};

struct SpeakerVerificationFeatures {
  bool valid{false};
  std::array<float, SpeakerVerificationTemplate::kFeatureCount> values{};
};

struct SpeakerVerificationDecision {
  bool enabled{false};
  bool template_available{false};
  bool has_sufficient_audio{false};
  bool accepted{false};
  float score{0.0f};
};

class SpeakerVerification final {
 public:
  static constexpr size_t kCaptureFrames =
      1500 / sesame::audio::kFrameDurationMs;

  explicit SpeakerVerification(SpeakerVerificationTemplate template_data);

  void set_enabled(bool enabled);
  bool enabled() const;
  void clear();
  void feed_pcm(const int16_t* pcm, size_t sample_count);
  SpeakerVerificationFeatures extract_latest() const;
  SpeakerVerificationDecision verify_latest() const;

 private:
  struct Frame {
    std::array<int16_t, sesame::audio::kSamplesPerFrame> samples{};
  };

  static constexpr size_t kMinimumVoicedFrames = 15;
  SpeakerVerificationTemplate template_{};
  bool enabled_{false};
  std::array<Frame, kCaptureFrames> frames_{};
  size_t oldest_{0};
  size_t size_{0};
};

static_assert(SpeakerVerification::kCaptureFrames == 75);

}  // namespace sesame::voice
