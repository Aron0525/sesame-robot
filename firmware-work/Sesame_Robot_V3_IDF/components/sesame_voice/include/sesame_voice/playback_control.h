#pragma once

#include <atomic>
#include <cstdint>

namespace sesame::voice {

class PlaybackControl final {
 public:
  void begin(uint32_t generation_id) {
    paused_.store(false, std::memory_order_relaxed);
    generation_id_.store(generation_id, std::memory_order_release);
  }

  bool set_paused(uint32_t generation_id, bool paused) {
    if (generation_id == 0 ||
        generation_id_.load(std::memory_order_acquire) != generation_id) {
      return false;
    }
    paused_.store(paused, std::memory_order_release);
    return true;
  }

  bool paused() const { return paused_.load(std::memory_order_acquire); }

  uint32_t generation_id() const {
    return generation_id_.load(std::memory_order_acquire);
  }

  void clear() {
    paused_.store(false, std::memory_order_relaxed);
    generation_id_.store(0, std::memory_order_release);
  }

 private:
  std::atomic<uint32_t> generation_id_{0};
  std::atomic<bool> paused_{false};
};

}  // namespace sesame::voice
