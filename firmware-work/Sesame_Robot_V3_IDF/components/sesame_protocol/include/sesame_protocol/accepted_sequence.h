#pragma once

#include <cstdint>

namespace sesame::protocol {

// A wire sequence advances only after the corresponding frame is accepted by
// its bounded outbound queue. A rejected enqueue can therefore be retried or
// fault the session without creating an invisible protocol gap.
class AcceptedSequence final {
 public:
  uint32_t current() const { return value_; }

  bool commit_if(bool accepted) {
    if (accepted) ++value_;
    return accepted;
  }

  void reset() { value_ = 0; }

 private:
  uint32_t value_{0};
};

}  // namespace sesame::protocol
