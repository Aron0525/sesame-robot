#pragma once

#include "driver/gpio.h"

#include "sesame_audio/audio_contract.h"

namespace sesame::audio {

inline constexpr gpio_num_t kMicrophoneBclkPin =
    static_cast<gpio_num_t>(kMicrophoneBclkGpio);
inline constexpr gpio_num_t kMicrophoneWsPin =
    static_cast<gpio_num_t>(kMicrophoneWsGpio);
inline constexpr gpio_num_t kMicrophoneDataPin =
    static_cast<gpio_num_t>(kMicrophoneDataGpio);
inline constexpr gpio_num_t kSpeakerBclkPin =
    static_cast<gpio_num_t>(kSpeakerBclkGpio);
inline constexpr gpio_num_t kSpeakerWsPin =
    static_cast<gpio_num_t>(kSpeakerWsGpio);
inline constexpr gpio_num_t kSpeakerDataPin =
    static_cast<gpio_num_t>(kSpeakerDataGpio);

}  // namespace sesame::audio
