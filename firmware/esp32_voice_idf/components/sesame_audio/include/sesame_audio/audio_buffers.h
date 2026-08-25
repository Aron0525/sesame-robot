#pragma once

#include <array>
#include <cstddef>

namespace sesame::audio {

template <typename Item, size_t Capacity>
class FixedRingBuffer {
 public:
  static_assert(Capacity > 0);

  bool push(const Item& item) {
    if (size_ == Capacity) {
      return false;
    }
    items_[tail_] = item;
    tail_ = (tail_ + 1) % Capacity;
    ++size_;
    return true;
  }

  bool pop(Item* output) {
    if (output == nullptr || size_ == 0) {
      return false;
    }
    *output = items_[head_];
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
  constexpr size_t capacity() const { return Capacity; }
  bool empty() const { return size_ == 0; }
  bool full() const { return size_ == Capacity; }

 private:
  std::array<Item, Capacity> items_{};
  size_t head_{0};
  size_t tail_{0};
  size_t size_{0};
};

}  // namespace sesame::audio
