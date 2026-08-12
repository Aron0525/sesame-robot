#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_voice/pcm_preroll_buffer.h"

int main() {
  using sesame::audio::kSamplesPerFrame;
  using sesame::voice::PcmPreRollBuffer;

  PcmPreRollBuffer buffer;
  std::array<int16_t, kSamplesPerFrame> pcm{};

  // The wait window keeps exactly the newest 500 ms (25 x 20-ms frames).
  for (uint64_t frame = 0; frame < 30; ++frame) {
    pcm.fill(static_cast<int16_t>(frame));
    buffer.push(pcm.data(), pcm.size(), frame * 20);
  }
  assert(buffer.size() == 25);
  assert(buffer.duration_ms() == 500);

  PcmPreRollBuffer::Frame output{};
  assert(buffer.pop_oldest(&output));
  assert(output.timestamp_ms == 100);
  assert(output.samples.front() == 5);

  for (int expected = 6; expected < 30; ++expected) {
    assert(buffer.pop_oldest(&output));
    assert(output.samples.front() == expected);
  }
  assert(buffer.empty());
  assert(!buffer.pop_oldest(&output));

  pcm.fill(42);
  buffer.push(pcm.data(), pcm.size(), 999);
  buffer.clear();
  assert(buffer.empty());
}
