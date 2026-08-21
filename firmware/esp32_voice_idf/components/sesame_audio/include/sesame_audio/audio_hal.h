#pragma once

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

  esp_err_t read_microphone_frame(int16_t* output, size_t output_samples,
                                  uint32_t timeout_ms);
  esp_err_t write_speaker_frame(const int16_t* input, size_t input_samples,
                                uint32_t timeout_ms);

  // Hat SPK2 has no separate SD/EN line; retained as a no-op lifecycle hook.
  esp_err_t set_amplifier_enabled(bool enabled);
  bool initialized() const { return initialized_; }

 private:
  i2s_chan_handle_t tx_channel_{nullptr};
  i2s_chan_handle_t rx_channel_{nullptr};
  bool initialized_{false};
};

}  // namespace sesame::audio
