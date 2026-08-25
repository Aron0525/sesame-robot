#pragma once

#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#endif

#include "sesame_audio/audio_contract.h"

namespace sesame::audio {

class OpusCodec {
 public:
  static constexpr size_t kPcmSamplesPerPacket = kSamplesPerFrame;
  static constexpr size_t kPcmBytesPerPacket = kPcmBytesPerFrame;
  static constexpr size_t kMaxPacketBytes = 400;

  static constexpr bool is_valid_packet_size(size_t size) {
    return size > 0 && size <= kMaxPacketBytes;
  }

#ifdef ESP_PLATFORM
  OpusCodec() = default;
  ~OpusCodec();

  OpusCodec(const OpusCodec&) = delete;
  OpusCodec& operator=(const OpusCodec&) = delete;

  esp_err_t initialize();
  void shutdown();
  esp_err_t reset();

  esp_err_t encode(const int16_t* pcm, size_t samples, uint8_t* packet,
                   size_t packet_capacity, size_t* packet_size);
  esp_err_t decode(const uint8_t* packet, size_t packet_size, int16_t* pcm,
                   size_t pcm_capacity_samples, size_t* decoded_samples);

  bool initialized() const { return encoder_ != nullptr && decoder_ != nullptr; }

 private:
  void* encoder_{nullptr};
  void* decoder_{nullptr};
  int encoder_input_bytes_{0};
  int encoder_output_bytes_{0};
#endif
};

}  // namespace sesame::audio
