#pragma once

#include "driver/gpio.h"

#include "sesame_audio/audio_contract.h"

namespace sesame::audio {

// The INMP441 and MAX98357A share the I2S clocks. Each has an independent
// data line, so ESP32-S3 can create one full-duplex I2S channel pair.
inline constexpr gpio_num_t kI2sBclkPin =
    static_cast<gpio_num_t>(kI2sBclkGpio);
inline constexpr gpio_num_t kI2sWsPin =
    static_cast<gpio_num_t>(kI2sWsGpio);
inline constexpr gpio_num_t kMicrophoneDataPin =
    static_cast<gpio_num_t>(kMicrophoneDataGpio);
inline constexpr gpio_num_t kSpeakerDataPin =
    static_cast<gpio_num_t>(kSpeakerDataGpio);
inline constexpr gpio_num_t kAmplifierEnablePin =
    static_cast<gpio_num_t>(kAmplifierEnableGpio);

}  // namespace sesame::audio
