#include "sesame_protocol/audio_frame.h"

#include <cstring>
#include <limits>

namespace sesame::protocol {
namespace {

constexpr uint8_t kMagic[] = {'S', 'S', 'M', '1'};

void write_u16_be(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value >> 8);
  output[1] = static_cast<uint8_t>(value);
}

void write_u32_be(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value >> 24);
  output[1] = static_cast<uint8_t>(value >> 16);
  output[2] = static_cast<uint8_t>(value >> 8);
  output[3] = static_cast<uint8_t>(value);
}

void write_u64_be(uint8_t* output, uint64_t value) {
  for (size_t index = 0; index < 8; ++index) {
    output[index] =
        static_cast<uint8_t>(value >> (56U - static_cast<unsigned>(index * 8)));
  }
}

uint16_t read_u16_be(const uint8_t* input) {
  return static_cast<uint16_t>(
      (static_cast<uint16_t>(input[0]) << 8) |
      static_cast<uint16_t>(input[1]));
}

uint32_t read_u32_be(const uint8_t* input) {
  return (static_cast<uint32_t>(input[0]) << 24) |
         (static_cast<uint32_t>(input[1]) << 16) |
         (static_cast<uint32_t>(input[2]) << 8) |
         static_cast<uint32_t>(input[3]);
}

uint64_t read_u64_be(const uint8_t* input) {
  uint64_t value = 0;
  for (size_t index = 0; index < 8; ++index) {
    value = (value << 8) | input[index];
  }
  return value;
}

bool is_valid_direction(uint8_t value) {
  return value == static_cast<uint8_t>(AudioDirection::kUplink) ||
         value == static_cast<uint8_t>(AudioDirection::kDownlink);
}

}  // namespace

AudioFrameError pack_audio_frame(const AudioFrame& frame, uint8_t* output,
                                 size_t output_capacity,
                                 size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }
  if (output == nullptr || bytes_written == nullptr) {
    return AudioFrameError::kNullArgument;
  }
  if (frame.payload == nullptr || frame.payload_size == 0) {
    return AudioFrameError::kEmptyPayload;
  }
  if (frame.payload_size > kMaxOpusPacketBytes ||
      frame.payload_size > std::numeric_limits<uint32_t>::max()) {
    return AudioFrameError::kPayloadTooLarge;
  }

  const size_t required_size = kAudioHeaderSize + frame.payload_size;
  if (output_capacity < required_size) {
    return AudioFrameError::kOutputTooSmall;
  }

  std::memcpy(output, kMagic, sizeof(kMagic));
  output[4] = kAudioProtocolVersion;
  output[5] = static_cast<uint8_t>(frame.direction);
  write_u16_be(output + 6, frame.flags);
  write_u32_be(output + 8, frame.stream_id);
  write_u32_be(output + 12, frame.generation_id);
  write_u32_be(output + 16, frame.sequence);
  write_u64_be(output + 20, frame.timestamp_ms);
  write_u32_be(output + 28, static_cast<uint32_t>(frame.payload_size));
  std::memcpy(output + kAudioHeaderSize, frame.payload, frame.payload_size);
  *bytes_written = required_size;
  return AudioFrameError::kOk;
}

AudioFrameError unpack_audio_frame(const uint8_t* message, size_t message_size,
                                   AudioFrame* output) {
  if (message == nullptr || output == nullptr) {
    return AudioFrameError::kNullArgument;
  }
  if (message_size < kAudioHeaderSize) {
    return AudioFrameError::kFrameTooShort;
  }
  if (std::memcmp(message, kMagic, sizeof(kMagic)) != 0) {
    return AudioFrameError::kInvalidMagic;
  }
  if (message[4] != kAudioProtocolVersion) {
    return AudioFrameError::kUnsupportedVersion;
  }
  if (!is_valid_direction(message[5])) {
    return AudioFrameError::kInvalidDirection;
  }

  const uint32_t payload_length = read_u32_be(message + 28);
  if (payload_length == 0) {
    return AudioFrameError::kEmptyPayload;
  }
  if (payload_length > kMaxOpusPacketBytes) {
    return AudioFrameError::kPayloadTooLarge;
  }
  if (payload_length != message_size - kAudioHeaderSize) {
    return AudioFrameError::kPayloadLengthMismatch;
  }

  output->direction = static_cast<AudioDirection>(message[5]);
  output->flags = read_u16_be(message + 6);
  output->stream_id = read_u32_be(message + 8);
  output->generation_id = read_u32_be(message + 12);
  output->sequence = read_u32_be(message + 16);
  output->timestamp_ms = read_u64_be(message + 20);
  output->payload = message + kAudioHeaderSize;
  output->payload_size = payload_length;
  return AudioFrameError::kOk;
}

const char* to_string(AudioFrameError error) {
  switch (error) {
    case AudioFrameError::kOk:
      return "ok";
    case AudioFrameError::kNullArgument:
      return "null argument";
    case AudioFrameError::kOutputTooSmall:
      return "output too small";
    case AudioFrameError::kFrameTooShort:
      return "frame too short";
    case AudioFrameError::kInvalidMagic:
      return "invalid magic";
    case AudioFrameError::kUnsupportedVersion:
      return "unsupported version";
    case AudioFrameError::kInvalidDirection:
      return "invalid direction";
    case AudioFrameError::kEmptyPayload:
      return "empty payload";
    case AudioFrameError::kPayloadTooLarge:
      return "payload too large";
    case AudioFrameError::kPayloadLengthMismatch:
      return "payload length mismatch";
  }
  return "unknown";
}

}  // namespace sesame::protocol
