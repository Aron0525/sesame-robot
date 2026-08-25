#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "sesame_audio/audio_contract.h"

namespace sesame::voice {

struct PcmFrame {
  uint32_t generation_id{0};
  uint32_t sequence{0};
  std::array<int16_t, sesame::audio::kSamplesPerFrame> samples{};
};

enum class PlaybackPushResult {
  kAccepted = 0,
  kStaleGeneration,
  kSequenceGap,
  kFull,
};

template <size_t Capacity>
class PlaybackBuffer {
 public:
  void begin_generation(uint32_t generation_id) {
    generation_id_ = generation_id;
    expected_sequence_ = 0;
    clear();
  }

  PlaybackPushResult push(const PcmFrame& frame) {
    if (frame.generation_id != generation_id_) {
      return PlaybackPushResult::kStaleGeneration;
    }
    if (expected_sequence_ != 0 && frame.sequence != expected_sequence_) {
      clear();
      expected_sequence_ = frame.sequence + 1;
      return PlaybackPushResult::kSequenceGap;
    }
    if (size_ == Capacity) return PlaybackPushResult::kFull;
    frames_[tail_] = frame;
    tail_ = (tail_ + 1) % Capacity;
    ++size_;
    expected_sequence_ = frame.sequence + 1;
    return PlaybackPushResult::kAccepted;
  }

  bool pop(PcmFrame* output) {
    if (output == nullptr || size_ == 0) return false;
    *output = frames_[head_];
    head_ = (head_ + 1) % Capacity;
    --size_;
    return true;
  }

  void clear() {
    head_ = 0;
    tail_ = 0;
    size_ = 0;
  }

  size_t size() const { return size_; }
  uint32_t generation_id() const { return generation_id_; }

 private:
  std::array<PcmFrame, Capacity> frames_{};
  size_t head_{0};
  size_t tail_{0};
  size_t size_{0};
  uint32_t generation_id_{0};
  uint32_t expected_sequence_{0};
};

}  // namespace sesame::voice
