#include "sesame_voice/pcm_preroll_buffer.h"

#include <cstring>

namespace sesame::voice {

void PcmPreRollBuffer::clear() {
  oldest_ = 0;
  size_ = 0;
}

void PcmPreRollBuffer::push(const int16_t* samples, size_t sample_count,
                            uint64_t timestamp_ms) {
  if (samples == nullptr || sample_count != sesame::audio::kSamplesPerFrame) {
    return;
  }

  size_t destination = (oldest_ + size_) % frames_.size();
  if (size_ == frames_.size()) {
    destination = oldest_;
    oldest_ = (oldest_ + 1) % frames_.size();
  } else {
    ++size_;
  }
  Frame& frame = frames_[destination];
  std::memcpy(frame.samples.data(), samples,
              frame.samples.size() * sizeof(frame.samples[0]));
  frame.timestamp_ms = timestamp_ms;
}

bool PcmPreRollBuffer::pop_oldest(Frame* output) {
  if (output == nullptr || empty()) return false;
  *output = frames_[oldest_];
  oldest_ = (oldest_ + 1) % frames_.size();
  --size_;
  if (empty()) oldest_ = 0;
  return true;
}

}  // namespace sesame::voice
