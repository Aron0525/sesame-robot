#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_audio/audio_contract.h"
#include "sesame_voice/speaker_verification.h"

namespace {

using Verification = sesame::voice::SpeakerVerification;

std::array<int16_t, sesame::audio::kSamplesPerFrame> voiced_frame(
    uint32_t phase) {
  std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
  for (size_t index = 0; index < pcm.size(); ++index) {
    const uint32_t sample = phase + static_cast<uint32_t>(index);
    const int32_t carrier = (sample % 73U < 36U) ? 6000 : -6000;
    const int32_t harmonic = (sample % 29U < 14U) ? 1900 : -1900;
    pcm[index] = static_cast<int16_t>(carrier + harmonic);
  }
  return pcm;
}

void feed_voice(Verification* verification) {
  assert(verification != nullptr);
  for (size_t frame = 0; frame < Verification::kCaptureFrames; ++frame) {
    const auto pcm = voiced_frame(static_cast<uint32_t>(
        frame * sesame::audio::kSamplesPerFrame));
    verification->feed_pcm(pcm.data(), pcm.size());
  }
}

}  // namespace

int main() {
  using sesame::voice::SpeakerVerification;
  using sesame::voice::SpeakerVerificationTemplate;

  // The feature is opt-in. With it disabled, phrase recognition proceeds
  // without requiring an enrolled biometric template.
  SpeakerVerification disabled({});
  assert(!disabled.enabled());
  assert(disabled.verify_latest().accepted);

  // Enabling verification without a local template fails closed.
  disabled.set_enabled(true);
  feed_voice(&disabled);
  const auto unavailable = disabled.verify_latest();
  assert(unavailable.enabled);
  assert(!unavailable.template_available);
  assert(!unavailable.accepted);

  // Extract a reference through the production feature implementation, then
  // verify the same fixed phrase capture.
  SpeakerVerification enrollment({});
  feed_voice(&enrollment);
  const auto features = enrollment.extract_latest();
  assert(features.valid);
  SpeakerVerificationTemplate template_data{};
  template_data.available = true;
  template_data.threshold = 0.95f;
  template_data.centroid = features.values;

  SpeakerVerification matched(template_data);
  matched.set_enabled(true);
  feed_voice(&matched);
  const auto accepted = matched.verify_latest();
  assert(accepted.enabled);
  assert(accepted.template_available);
  assert(accepted.has_sufficient_audio);
  assert(accepted.accepted);
  assert(accepted.score >= template_data.threshold);

  // Silence cannot become a positive speaker decision.
  SpeakerVerification silence(template_data);
  silence.set_enabled(true);
  std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
  for (size_t frame = 0; frame < SpeakerVerification::kCaptureFrames; ++frame) {
    silence.feed_pcm(pcm.data(), pcm.size());
  }
  const auto rejected = silence.verify_latest();
  assert(!rejected.has_sufficient_audio);
  assert(!rejected.accepted);
}
