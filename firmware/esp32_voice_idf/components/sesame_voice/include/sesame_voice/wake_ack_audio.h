#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::voice {

// 16 kHz signed PCM generated locally from the fixed phrase "我在".
const int16_t* wake_ack_audio_samples();
size_t wake_ack_audio_sample_count();

}  // namespace sesame::voice
