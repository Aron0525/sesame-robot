#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::protocol {

inline constexpr uint8_t kAudioProtocolVersion = 1;
inline constexpr size_t kAudioHeaderSize = 32;
inline constexpr size_t kMaxOpusPacketBytes = 1500;
inline constexpr uint16_t kAudioFlagPcmS16Le = 0x0001;

enum class AudioDirection : uint8_t {
  kUplink = 0,
  kDownlink = 1,
};

enum class AudioFrameError {
  kOk = 0,
  kNullArgument,
  kOutputTooSmall,
  kFrameTooShort,
  kInvalidMagic,
  kUnsupportedVersion,
  kInvalidDirection,
  kEmptyPayload,
  kPayloadTooLarge,
  kPayloadLengthMismatch,
};

struct AudioFrame {
  AudioDirection direction{AudioDirection::kUplink};
  uint16_t flags{0};
  uint32_t stream_id{0};
  uint32_t generation_id{0};
  uint32_t sequence{0};
  uint64_t timestamp_ms{0};
  const uint8_t* payload{nullptr};
  size_t payload_size{0};
};

AudioFrameError pack_audio_frame(const AudioFrame& frame, uint8_t* output,
                                 size_t output_capacity,
                                 size_t* bytes_written);

AudioFrameError unpack_audio_frame(const uint8_t* message, size_t message_size,
                                   AudioFrame* output);

const char* to_string(AudioFrameError error);

}  // namespace sesame::protocol
