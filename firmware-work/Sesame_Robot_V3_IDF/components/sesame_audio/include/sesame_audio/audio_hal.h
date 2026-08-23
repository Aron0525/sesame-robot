#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/i2s_std.h"
#include "esp_err.h"

#include "sesame_audio/audio_config.h"

namespace sesame::audio {

class AudioHal {
 public:
  AudioHal() = default;
  ~AudioHal();

  AudioHal(const AudioHal&) = delete;
  AudioHal& operator=(const AudioHal&) = delete;

  esp_err_t initialize();
  esp_err_t shutdown();

  esp_err_t reset_microphone_capture();
  esp_err_t read_microphone_frame(int16_t* output, size_t output_samples,
                                  uint32_t timeout_ms);
  esp_err_t write_speaker_frame(const int16_t* input, size_t input_samples,
                                uint32_t timeout_ms);

  bool initialized() const { return initialized_; }

 private:
  i2s_chan_handle_t tx_channel_{nullptr};
  i2s_chan_handle_t rx_channel_{nullptr};
  // These DMA conversion buffers live with the static AudioHal object instead
  // of consuming the microphone/playback task stacks on every 20-ms frame.
  std::array<int32_t, kRawI2sSamplesPerFrame> microphone_raw_{};
  std::array<int32_t, kRawI2sSamplesPerFrame> speaker_raw_{};
  bool initialized_{false};
};

}  // namespace sesame::audio
