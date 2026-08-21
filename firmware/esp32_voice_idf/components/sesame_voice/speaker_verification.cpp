#include "sesame_voice/speaker_verification.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sesame::voice {
namespace {

constexpr std::array<size_t, SpeakerVerificationTemplate::kFeatureCount - 1>
    kAutocorrelationLags = {8, 12, 16, 20, 24, 32,
                            40, 48, 56, 64, 72, 80};
constexpr float kMinimumRms = 80.0f;
constexpr float kRelativeVoiceFloor = 0.13f;
constexpr float kEpsilon = 1.0e-6f;

using PcmFrame = std::array<int16_t, sesame::audio::kSamplesPerFrame>;

float frame_rms(const PcmFrame& samples) {
  double sum = 0.0;
  for (const int16_t sample : samples) {
    const double value = static_cast<double>(sample);
    sum += value * value;
  }
  return static_cast<float>(
      std::sqrt(sum / static_cast<double>(samples.size())));
}

float normalized_autocorrelation(const PcmFrame& samples, size_t lag) {
  if (lag == 0 || lag >= samples.size()) return 0.0f;
  double cross = 0.0;
  double left_energy = 0.0;
  double right_energy = 0.0;
  for (size_t index = 0; index + lag < samples.size(); ++index) {
    const double left = static_cast<double>(samples[index]);
    const double right = static_cast<double>(samples[index + lag]);
    cross += left * right;
    left_energy += left * left;
    right_energy += right * right;
  }
  const double denominator = std::sqrt(left_energy * right_energy);
  return denominator <= 0.0 ? 0.0f : static_cast<float>(cross / denominator);
}

float zero_crossing_rate(const PcmFrame& samples) {
  size_t crossings = 0;
  for (size_t index = 1; index < samples.size(); ++index) {
    const int16_t previous = samples[index - 1];
    const int16_t current = samples[index];
    if ((previous < 0 && current >= 0) || (previous >= 0 && current < 0)) {
      ++crossings;
    }
  }
  return static_cast<float>(crossings) /
         static_cast<float>(samples.size() - 1);
}

}  // namespace

SpeakerVerification::SpeakerVerification(
    SpeakerVerificationTemplate template_data)
    : template_(template_data) {}

void SpeakerVerification::set_enabled(bool enabled) {
  enabled_ = enabled;
  clear();
}

bool SpeakerVerification::enabled() const { return enabled_; }

void SpeakerVerification::clear() {
  oldest_ = 0;
  size_ = 0;
}

void SpeakerVerification::feed_pcm(const int16_t* pcm, size_t sample_count) {
  if (pcm == nullptr || sample_count != sesame::audio::kSamplesPerFrame) {
    return;
  }
  size_t destination = (oldest_ + size_) % frames_.size();
  if (size_ == frames_.size()) {
    destination = oldest_;
    oldest_ = (oldest_ + 1) % frames_.size();
  } else {
    ++size_;
  }
  std::memcpy(frames_[destination].samples.data(), pcm,
              frames_[destination].samples.size() * sizeof(int16_t));
}

SpeakerVerificationFeatures SpeakerVerification::extract_latest() const {
  SpeakerVerificationFeatures output{};
  if (size_ < kMinimumVoicedFrames) return output;

  float peak_rms = 0.0f;
  for (size_t offset = 0; offset < size_; ++offset) {
    const Frame& frame = frames_[(oldest_ + offset) % frames_.size()];
    peak_rms = std::max(peak_rms, frame_rms(frame.samples));
  }
  const float voice_floor =
      std::max(kMinimumRms, peak_rms * kRelativeVoiceFloor);

  size_t voiced_frames = 0;
  for (size_t offset = 0; offset < size_; ++offset) {
    const Frame& frame = frames_[(oldest_ + offset) % frames_.size()];
    if (frame_rms(frame.samples) < voice_floor) continue;
    output.values[0] += zero_crossing_rate(frame.samples);
    for (size_t feature = 0; feature < kAutocorrelationLags.size(); ++feature) {
      output.values[feature + 1] +=
          normalized_autocorrelation(frame.samples,
                                     kAutocorrelationLags[feature]);
    }
    ++voiced_frames;
  }
  if (voiced_frames < kMinimumVoicedFrames) return output;

  for (float& value : output.values) {
    value /= static_cast<float>(voiced_frames);
  }
  output.valid = true;
  return output;
}

SpeakerVerificationDecision SpeakerVerification::verify_latest() const {
  SpeakerVerificationDecision decision{};
  decision.enabled = enabled_;
  if (!enabled_) {
    decision.accepted = true;
    return decision;
  }

  decision.template_available = template_.available;
  if (!template_.available || template_.threshold <= 0.0f ||
      template_.threshold > 1.0f) {
    return decision;
  }
  const SpeakerVerificationFeatures features = extract_latest();
  decision.has_sufficient_audio = features.valid;
  if (!features.valid) return decision;

  double dot = 0.0;
  double feature_norm = 0.0;
  double template_norm = 0.0;
  for (size_t index = 0; index < features.values.size(); ++index) {
    const double feature = features.values[index];
    const double reference = template_.centroid[index];
    dot += feature * reference;
    feature_norm += feature * feature;
    template_norm += reference * reference;
  }
  const double denominator = std::sqrt(feature_norm * template_norm);
  if (denominator <= kEpsilon) return decision;
  decision.score = static_cast<float>(dot / denominator);
  decision.accepted = decision.score >= template_.threshold;
  return decision;
}

}  // namespace sesame::voice
