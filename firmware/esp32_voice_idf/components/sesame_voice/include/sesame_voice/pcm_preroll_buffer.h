#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "sesame_audio/audio_contract.h"

namespace sesame::voice {

// Keeps the newest 500 ms of microphone PCM while VAD is waiting for speech.
// The controller also uses it as a short catch-up queue after speech starts,
// preventing the first syllable from being lost to asynchronous VAD latency.
class PcmPreRollBuffer final {
 public:
  static constexpr size_t kCapacityFrames =
      500 / sesame::audio::kFrameDurationMs;

  struct Frame {
    std::array<int16_t, sesame::audio::kSamplesPerFrame> samples{};
    uint64_t timestamp_ms{0};
  };

  void clear();
  void push(const int16_t* samples, size_t sample_count,
            uint64_t timestamp_ms);
  bool pop_oldest(Frame* output);

  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }
  uint32_t duration_ms() const {
    return static_cast<uint32_t>(size_ * sesame::audio::kFrameDurationMs);
  }

 private:
  std::array<Frame, kCapacityFrames> frames_{};
  size_t oldest_{0};
  size_t size_{0};
};

static_assert(PcmPreRollBuffer::kCapacityFrames == 25);

}  // namespace sesame::voice
