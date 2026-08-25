#pragma once

#include <cstdint>

namespace sesame::protocol {

class SequenceTracker {
 public:
  void reset(uint32_t stream_id, uint32_t generation_id) {
    stream_id_ = stream_id;
    generation_id_ = generation_id;
    expected_sequence_ = 0;
    active_ = true;
  }

  bool accept(uint32_t stream_id, uint32_t generation_id, uint32_t sequence) {
    if (!active_ || stream_id != stream_id_ ||
        generation_id != generation_id_ || sequence != expected_sequence_) {
      return false;
    }
    ++expected_sequence_;
    return true;
  }

  void clear() {
    active_ = false;
    expected_sequence_ = 0;
  }

  uint32_t expected_sequence() const { return expected_sequence_; }

 private:
  uint32_t stream_id_{0};
  uint32_t generation_id_{0};
  uint32_t expected_sequence_{0};
  bool active_{false};
};

}  // namespace sesame::protocol
