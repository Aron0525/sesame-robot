#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "sesame_protocol/audio_frame.h"

using sesame::protocol::AudioDirection;
using sesame::protocol::AudioFrame;
using sesame::protocol::AudioFrameError;

namespace {

void test_round_trip_preserves_network_fields() {
  const std::array<uint8_t, 4> opus = {0xF8, 0xFF, 0xFE, 0x00};
  AudioFrame source{
      .direction = AudioDirection::kUplink,
      .flags = 0,
      .stream_id = 7,
      .generation_id = 0,
      .sequence = 42,
      .timestamp_ms = 123456789ULL,
      .payload = opus.data(),
      .payload_size = opus.size(),
  };

  std::array<uint8_t, sesame::protocol::kAudioHeaderSize + opus.size()> wire{};
  size_t written = 0;
  assert(sesame::protocol::pack_audio_frame(source, wire.data(), wire.size(),
                                            &written) ==
         AudioFrameError::kOk);
  assert(written == wire.size());
  assert(std::memcmp(wire.data(), "SSM1", 4) == 0);

  AudioFrame decoded{};
  assert(sesame::protocol::unpack_audio_frame(wire.data(), wire.size(),
                                              &decoded) ==
         AudioFrameError::kOk);
  assert(decoded.direction == AudioDirection::kUplink);
  assert(decoded.stream_id == 7);
  assert(decoded.sequence == 42);
  assert(decoded.timestamp_ms == 123456789ULL);
  assert(decoded.payload_size == opus.size());
  assert(std::memcmp(decoded.payload, opus.data(), opus.size()) == 0);
}

void test_rejects_wrong_magic_and_length() {
  std::array<uint8_t, sesame::protocol::kAudioHeaderSize + 1> wire{};
  std::memcpy(wire.data(), "BAD1", 4);
  wire[4] = sesame::protocol::kAudioProtocolVersion;
  wire[5] = static_cast<uint8_t>(AudioDirection::kDownlink);
  wire[31] = 1;

  AudioFrame decoded{};
  assert(sesame::protocol::unpack_audio_frame(wire.data(), wire.size(),
                                              &decoded) ==
         AudioFrameError::kInvalidMagic);

  std::memcpy(wire.data(), "SSM1", 4);
  wire[31] = 2;
  assert(sesame::protocol::unpack_audio_frame(wire.data(), wire.size(),
                                              &decoded) ==
         AudioFrameError::kPayloadLengthMismatch);
}

void test_rejects_oversized_payload() {
  std::array<uint8_t, sesame::protocol::kMaxOpusPacketBytes + 1> opus{};
  AudioFrame source{
      .direction = AudioDirection::kUplink,
      .payload = opus.data(),
      .payload_size = opus.size(),
  };
  std::array<uint8_t, 1600> wire{};
  size_t written = 99;

  assert(sesame::protocol::pack_audio_frame(source, wire.data(), wire.size(),
                                            &written) ==
         AudioFrameError::kPayloadTooLarge);
  assert(written == 0);
}

}  // namespace

int main() {
  test_round_trip_preserves_network_fields();
  test_rejects_wrong_magic_and_length();
  test_rejects_oversized_payload();
  return 0;
}
