#include <cassert>

#include "sesame_audio/opus_codec.h"

int main() {
  using sesame::audio::OpusCodec;

  static_assert(OpusCodec::kPcmSamplesPerPacket == 320);
  static_assert(OpusCodec::kPcmBytesPerPacket == 640);
  static_assert(OpusCodec::kMaxPacketBytes <= 1500);
  assert(!OpusCodec::is_valid_packet_size(0));
  assert(OpusCodec::is_valid_packet_size(1));
  assert(OpusCodec::is_valid_packet_size(OpusCodec::kMaxPacketBytes));
  assert(!OpusCodec::is_valid_packet_size(OpusCodec::kMaxPacketBytes + 1));
}
