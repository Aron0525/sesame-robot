#pragma once

#include "driver/gpio.h"

#include "sesame_audio/audio_contract.h"

namespace sesame::audio {

// I2S0 RX drives only the INMP441 clock pins and consumes its data pin.
inline constexpr gpio_num_t kMicrophoneBclkPin =
    static_cast<gpio_num_t>(kMicrophoneBclkGpio);
inline constexpr gpio_num_t kMicrophoneWsPin =
    static_cast<gpio_num_t>(kMicrophoneWsGpio);
inline constexpr gpio_num_t kMicrophoneDataPin =
    static_cast<gpio_num_t>(kMicrophoneDataGpio);

// I2S1 TX drives only the M5Stack Hat SPK2 I2S pins.
inline constexpr gpio_num_t kSpeakerBclkPin =
    static_cast<gpio_num_t>(kSpeakerBclkGpio);
inline constexpr gpio_num_t kSpeakerWsPin =
    static_cast<gpio_num_t>(kSpeakerWsGpio);
inline constexpr gpio_num_t kSpeakerDataPin =
    static_cast<gpio_num_t>(kSpeakerDataGpio);

}  // namespace sesame::audio
