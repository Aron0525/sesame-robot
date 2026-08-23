#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace sesame::voice {

class PlaybackTurnBinding final {
 public:
  bool bind(const char* turn_id, uint32_t generation_id) {
    if (turn_id == nullptr || generation_id == 0) return false;
    const size_t length = strnlen(turn_id, turn_id_.size());
    if (length == 0 || length >= turn_id_.size()) return false;
    turn_id_.fill('\0');
    std::memcpy(turn_id_.data(), turn_id, length);
    generation_id_ = generation_id;
    return true;
  }

  bool matches(const char* turn_id, uint32_t generation_id) const {
    return turn_id != nullptr && generation_id_ == generation_id &&
           generation_id != 0 && turn_id_[0] != '\0' &&
           std::strcmp(turn_id_.data(), turn_id) == 0;
  }

  const char* turn_id() const {
    return turn_id_[0] == '\0' ? nullptr : turn_id_.data();
  }

  uint32_t generation_id() const { return generation_id_; }

  void clear() {
    turn_id_.fill('\0');
    generation_id_ = 0;
  }

 private:
  std::array<char, 101> turn_id_{};
  uint32_t generation_id_{0};
};

}  // namespace sesame::voice
