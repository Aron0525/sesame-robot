#include <array>
#include <cstdint>
#include <cstdio>

#include "sesame_protocol/audio_frame.h"

int main() {
  const std::array<uint8_t, 4> opus = {0xF8, 0xFF, 0xFE, 0x00};
  const sesame::protocol::AudioFrame frame{
      .direction = sesame::protocol::AudioDirection::kDownlink,
      .flags = 0,
      .stream_id = 2,
      .generation_id = 0x01020304,
      .sequence = 5,
      .timestamp_ms = 0x0102030405060708ULL,
      .payload = opus.data(),
      .payload_size = opus.size(),
  };
  std::array<uint8_t, sesame::protocol::kAudioHeaderSize + opus.size()> wire{};
  size_t written = 0;
  if (sesame::protocol::pack_audio_frame(frame, wire.data(), wire.size(),
                                         &written) !=
          sesame::protocol::AudioFrameError::kOk ||
      written != wire.size()) {
    return 1;
  }
  for (const uint8_t byte : wire) std::printf("%02x", byte);
  std::putchar('\n');
}
