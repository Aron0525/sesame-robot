#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_audio/audio_contract.h"
#include "sesame_voice/owner_voice_gate.h"

namespace {

std::array<int16_t, sesame::audio::kSamplesPerFrame> voiced_frame(
    uint32_t phase) {
  std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
  // A repeatable, non-silent voiced-like waveform with a changing formant
  // shape. The test intentionally does not rely on a template fixture.
  for (size_t index = 0; index < pcm.size(); ++index) {
    const uint32_t sample = phase + static_cast<uint32_t>(index);
    const int32_t carrier = (sample % 73U < 36U) ? 6000 : -6000;
    const int32_t harmonic = (sample % 29U < 14U) ? 1900 : -1900;
    pcm[index] = static_cast<int16_t>(carrier + harmonic);
  }
  return pcm;
}

std::array<int16_t, sesame::audio::kSamplesPerFrame> quiet_voiced_frame(
    uint32_t phase) {
  std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
  // Low-gain microphone speech can have a low whole-frame RMS while retaining
  // a real voiced peak. It must not be confused with digital silence.
  for (size_t index = 0; index < 70; ++index) {
    const uint32_t sample = phase + static_cast<uint32_t>(index);
    pcm[index] = static_cast<int16_t>((sample % 31U < 15U) ? 700 : -700);
  }
  return pcm;
}

void feed_voice(sesame::voice::OwnerVoiceGate* gate) {
  assert(gate != nullptr);
  for (size_t frame = 0; frame < sesame::voice::OwnerVoiceGate::kCaptureFrames;
       ++frame) {
    const auto pcm = voiced_frame(static_cast<uint32_t>(
        frame * sesame::audio::kSamplesPerFrame));
    gate->feed_pcm(pcm.data(), pcm.size());
  }
}

}  // namespace

int main() {
  using sesame::voice::OwnerVoiceGate;
  using sesame::voice::OwnerVoiceprintTemplate;

  // A missing enrolled template must fail closed: MultiNet alone is not
  // sufficient once owner verification has been enabled in production.
  OwnerVoiceGate unavailable({});
  feed_voice(&unavailable);
  assert(!unavailable.verify_latest().accepted);

  // Enroll an in-memory reference from one capture, then accept the same
  // voiced waveform using the exact production feature extractor.
  OwnerVoiceGate enrollment({});
  feed_voice(&enrollment);
  const auto features = enrollment.extract_latest();
  assert(features.valid);
  OwnerVoiceprintTemplate template_data{};
  template_data.enabled = true;
  template_data.threshold = 0.95f;
  template_data.centroid = features.values;

  OwnerVoiceGate owner(template_data);
  feed_voice(&owner);
  const auto matched = owner.verify_latest();
  assert(matched.has_sufficient_audio);
  assert(matched.accepted);
  assert(matched.score >= template_data.threshold);

  // The enrolment set contains low-gain recordings. A real but quiet voiced
  // frame is accepted when it is enrolled, rather than being discarded by an
  // arbitrary absolute RMS floor.
  OwnerVoiceGate quiet_enrollment({});
  for (size_t frame = 0; frame < OwnerVoiceGate::kCaptureFrames; ++frame) {
    const auto pcm = quiet_voiced_frame(static_cast<uint32_t>(
        frame * sesame::audio::kSamplesPerFrame));
    quiet_enrollment.feed_pcm(pcm.data(), pcm.size());
  }
  const auto quiet_features = quiet_enrollment.extract_latest();
  assert(quiet_features.valid);

  // Silence does not contain enough voiced PCM to be a speaker decision and
  // must never pass through to the wake acknowledgement.
  OwnerVoiceGate silence(template_data);
  std::array<int16_t, sesame::audio::kSamplesPerFrame> pcm{};
  for (size_t frame = 0; frame < OwnerVoiceGate::kCaptureFrames; ++frame) {
    silence.feed_pcm(pcm.data(), pcm.size());
  }
  const auto rejected = silence.verify_latest();
  assert(!rejected.has_sufficient_audio);
  assert(!rejected.accepted);
}
