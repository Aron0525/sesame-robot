#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "driver/i2s_std.h"
#include "sesame_audio/audio_contract.h"
#include "sesame_audio/audio_hal.h"

enum class FakeState { kRegistered, kReady, kRunning };

struct FakeI2sChannel {
  int instance_id;
  bool tx;
  i2s_chan_config_t channel_config;
  i2s_std_config_t standard_config{};
  FakeState state{FakeState::kRegistered};
  std::vector<int32_t> preloaded;
  std::vector<int32_t> written;
};

namespace {

std::vector<std::string> events;
FakeI2sChannel* current_tx = nullptr;
FakeI2sChannel* current_rx = nullptr;
int next_instance_id = 1;

void copy_words(const void* source, size_t size, std::vector<int32_t>* output) {
  assert(size % sizeof(int32_t) == 0);
  const auto* words = static_cast<const int32_t*>(source);
  output->insert(output->end(), words, words + size / sizeof(int32_t));
}

}  // namespace

esp_err_t i2s_new_channel(const i2s_chan_config_t* config,
                          i2s_chan_handle_t* tx,
                          i2s_chan_handle_t* rx) {
  assert(config != nullptr);
  assert((tx == nullptr) != (rx == nullptr));
  auto* channel = new FakeI2sChannel{
      .instance_id = next_instance_id++,
      .tx = tx != nullptr,
      .channel_config = *config,
  };
  if (tx != nullptr) {
    *tx = channel;
    current_tx = channel;
    events.emplace_back("new_tx");
  } else {
    *rx = channel;
    current_rx = channel;
    events.emplace_back("new_rx");
  }
  return ESP_OK;
}

esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel,
                                    const i2s_std_config_t* config) {
  if (channel == nullptr || config == nullptr ||
      channel->state != FakeState::kRegistered) {
    return ESP_ERR_INVALID_STATE;
  }
  channel->standard_config = *config;
  channel->state = FakeState::kReady;
  events.emplace_back(channel->tx ? "init_tx" : "init_rx");
  return ESP_OK;
}

esp_err_t i2s_channel_enable(i2s_chan_handle_t channel) {
  if (channel == nullptr || channel->state != FakeState::kReady) {
    return ESP_ERR_INVALID_STATE;
  }
  channel->state = FakeState::kRunning;
  events.emplace_back(channel->tx ? "enable_tx" : "enable_rx");
  return ESP_OK;
}

esp_err_t i2s_channel_disable(i2s_chan_handle_t channel) {
  if (channel == nullptr || channel->state != FakeState::kRunning) {
    return ESP_ERR_INVALID_STATE;
  }
  channel->state = FakeState::kReady;
  channel->preloaded.clear();
  events.emplace_back(channel->tx ? "disable_tx" : "disable_rx");
  return ESP_OK;
}

esp_err_t i2s_del_channel(i2s_chan_handle_t channel) {
  if (channel == nullptr || channel->state == FakeState::kRunning) {
    return ESP_ERR_INVALID_STATE;
  }
  events.emplace_back(channel->tx ? "delete_tx" : "delete_rx");
  if (channel == current_tx) current_tx = nullptr;
  if (channel == current_rx) current_rx = nullptr;
  delete channel;
  return ESP_OK;
}

esp_err_t i2s_channel_preload_data(i2s_chan_handle_t channel,
                                   const void* source,
                                   size_t size,
                                   size_t* bytes_loaded) {
  if (channel == nullptr || !channel->tx ||
      channel->state != FakeState::kReady) {
    return ESP_ERR_INVALID_STATE;
  }
  copy_words(source, size, &channel->preloaded);
  *bytes_loaded = size;
  events.emplace_back("preload_tx");
  return ESP_OK;
}

esp_err_t i2s_channel_write(i2s_chan_handle_t channel,
                            const void* source,
                            size_t size,
                            size_t* bytes_written,
                            uint32_t) {
  if (channel == nullptr || !channel->tx ||
      channel->state != FakeState::kRunning) {
    return ESP_ERR_INVALID_STATE;
  }
  copy_words(source, size, &channel->written);
  *bytes_written = size;
  events.emplace_back("write_tx");
  return ESP_OK;
}

esp_err_t i2s_channel_read(i2s_chan_handle_t channel,
                           void* destination,
                           size_t size,
                           size_t* bytes_read,
                           uint32_t) {
  if (channel == nullptr || channel->tx ||
      channel->state != FakeState::kRunning) {
    return ESP_ERR_INVALID_STATE;
  }
  std::memset(destination, 0, size);
  *bytes_read = size;
  return ESP_OK;
}

int main() {
  using namespace sesame::audio;

  AudioHal audio;
  assert(audio.initialize() == ESP_OK);
  assert(current_tx != nullptr);
  assert(current_rx != nullptr);
  assert(current_tx->state == FakeState::kReady);
  assert(current_rx->state == FakeState::kRunning);
  assert(current_tx->channel_config.dma_desc_num ==
         static_cast<int>(kSpeakerDmaDescriptorFrames));
  assert(current_tx->channel_config.dma_frame_num ==
         static_cast<int>(kSamplesPerFrame));
  assert(current_tx->standard_config.clk_cfg.sample_rate_hz == kSampleRateHz);
  assert(current_tx->standard_config.slot_cfg.data_bit_width == 32);
  assert(current_tx->standard_config.slot_cfg.slot_mode == I2S_SLOT_MODE_STEREO);
  assert(current_tx->standard_config.gpio_cfg.bclk == kSpeakerBclkGpio);
  assert(current_tx->standard_config.gpio_cfg.ws == kSpeakerWsGpio);
  assert(current_tx->standard_config.gpio_cfg.dout == kSpeakerDataGpio);
  assert(std::find(events.begin(), events.end(), "enable_tx") == events.end());

  std::array<int16_t, kSamplesPerFrame> pcm{};
  pcm[0] = 32767;
  pcm[1] = -32768;
  assert(audio.preload_speaker_frame(pcm.data(), pcm.size()) == ESP_OK);
  assert(current_tx->preloaded.size() == kRawI2sSamplesPerFrame);
  assert(current_tx->preloaded[0] == speaker_sample_to_i2s32(32767));
  assert(current_tx->preloaded[1] == speaker_sample_to_i2s32(32767));
  assert(current_tx->preloaded[2] == speaker_sample_to_i2s32(-32768));
  assert(current_tx->preloaded[3] == speaker_sample_to_i2s32(-32768));
  assert(audio.start_speaker() == ESP_OK);
  assert(current_tx->state == FakeState::kRunning);
  assert(audio.write_speaker_frame(pcm.data(), pcm.size(), 100) == ESP_OK);
  assert(current_tx->written.size() == kRawI2sSamplesPerFrame);
  assert(audio.stop_speaker() == ESP_OK);
  assert(current_tx->state == FakeState::kReady);

  const int prior_tx_id = current_tx->instance_id;
  assert(audio.preload_speaker_frame(pcm.data(), pcm.size()) == ESP_OK);
  assert(!current_tx->preloaded.empty());
  assert(audio.discard_speaker_preload() == ESP_OK);
  assert(current_tx != nullptr);
  assert(current_tx->instance_id != prior_tx_id);
  assert(current_tx->state == FakeState::kReady);
  assert(current_tx->preloaded.empty());
  assert(events[events.size() - 3] == "delete_tx");
  assert(events[events.size() - 2] == "new_tx");
  assert(events[events.size() - 1] == "init_tx");

  assert(audio.shutdown() == ESP_OK);
  assert(current_tx == nullptr);
  assert(current_rx == nullptr);
}
