#include "sesame_audio/audio_hal.h"
#include "sesame_audio/audio_volume.h"

#include <array>
#include <limits>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace sesame::audio {
namespace {

constexpr char kTag[] = "sesame_audio";
constexpr size_t kI2sSlotsPerFrame = 2;
constexpr size_t kRawSamplesPerFrame = kSamplesPerFrame * kI2sSlotsPerFrame;

int16_t saturate_to_pcm16(int32_t sample) {
  // INMP441 sends a signed 24-bit sample left-aligned in a 32-bit I2S slot.
  // Shift to S16LE while retaining enough headroom for close speech.
  const int32_t scaled = sample >> 14;
  if (scaled > std::numeric_limits<int16_t>::max()) {
    return std::numeric_limits<int16_t>::max();
  }
  if (scaled < std::numeric_limits<int16_t>::min()) {
    return std::numeric_limits<int16_t>::min();
  }
  return static_cast<int16_t>(scaled);
}

}  // namespace

AudioHal::~AudioHal() { shutdown(); }

esp_err_t AudioHal::initialize() {
  if (initialized_) {
    return ESP_OK;
  }

  const i2s_chan_config_t microphone_channel_config =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_RETURN_ON_ERROR(
      i2s_new_channel(&microphone_channel_config, nullptr, &rx_channel_),
      kTag, "create I2S0 microphone RX channel");

  i2s_std_config_t rx_config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = kMicrophoneBclkPin,
              .ws = kMicrophoneWsPin,
              .dout = I2S_GPIO_UNUSED,
              .din = kMicrophoneDataPin,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };
  esp_err_t result = i2s_channel_init_std_mode(rx_channel_, &rx_config);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }

  i2s_chan_config_t speaker_channel_config =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  // These settings reproduce the known-good Arduino MAX98357A test while
  // retaining I2S1 for the speaker because I2S0 is dedicated to INMP441 RX.
  speaker_channel_config.dma_desc_num = kSpeakerDmaDescriptorCount;
  speaker_channel_config.dma_frame_num = kSpeakerDmaFramesPerDescriptor;
  speaker_channel_config.auto_clear_after_cb = kSpeakerDmaAutoClear;
  result = i2s_new_channel(&speaker_channel_config, &tx_channel_, nullptr);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }

  i2s_std_config_t tx_config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = kSpeakerBclkPin,
              .ws = kSpeakerWsPin,
              .dout = kSpeakerDataPin,
              .din = I2S_GPIO_UNUSED,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };
  result = i2s_channel_init_std_mode(tx_channel_, &tx_config);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }

  result = i2s_channel_enable(rx_channel_);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }
  result = i2s_channel_enable(tx_channel_);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }

  initialized_ = true;
  ESP_LOGI(kTag,
           "I2S ready: I2S0 RX mic 14/47/48 (32-bit), I2S1 TX SPK2 1/2/3 (16-bit)");
  return ESP_OK;
}

esp_err_t AudioHal::shutdown() {
  if (tx_channel_ != nullptr) {
    i2s_channel_disable(tx_channel_);
    i2s_del_channel(tx_channel_);
    tx_channel_ = nullptr;
  }
  if (rx_channel_ != nullptr) {
    i2s_channel_disable(rx_channel_);
    i2s_del_channel(rx_channel_);
    rx_channel_ = nullptr;
  }
  initialized_ = false;
  return ESP_OK;
}

esp_err_t AudioHal::read_microphone_frame(int16_t* output,
                                          size_t output_samples,
                                          uint32_t timeout_ms) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (output == nullptr || output_samples != kSamplesPerFrame) {
    return ESP_ERR_INVALID_ARG;
  }

  std::array<int32_t, kRawSamplesPerFrame> raw{};
  size_t bytes_read = 0;
  ESP_RETURN_ON_ERROR(
      i2s_channel_read(rx_channel_, raw.data(), sizeof(raw), &bytes_read,
                       pdMS_TO_TICKS(timeout_ms)),
      kTag, "read microphone frame");
  if (bytes_read != sizeof(raw)) {
    return ESP_ERR_INVALID_SIZE;
  }

  for (size_t frame = 0; frame < kSamplesPerFrame; ++frame) {
    // INMP441 L/R is tied to GND, therefore valid samples occupy left slot 0.
    output[frame] = saturate_to_pcm16(raw[frame * kI2sSlotsPerFrame]);
  }
  return ESP_OK;
}

esp_err_t AudioHal::write_speaker_frame(const int16_t* input,
                                        size_t input_samples,
                                        uint32_t timeout_ms) {
  if (!initialized_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (input == nullptr || input_samples != kSamplesPerFrame) {
    return ESP_ERR_INVALID_ARG;
  }

  std::array<int16_t, kRawSamplesPerFrame> raw{};
  for (size_t frame = 0; frame < kSamplesPerFrame; ++frame) {
    const int16_t adjusted = scale_speaker_pcm16(input[frame]);
    raw[frame * kI2sSlotsPerFrame] = adjusted;
    raw[frame * kI2sSlotsPerFrame + 1] = adjusted;
  }

  size_t bytes_written = 0;
  ESP_RETURN_ON_ERROR(
      i2s_channel_write(tx_channel_, raw.data(), sizeof(raw), &bytes_written,
                        pdMS_TO_TICKS(timeout_ms)),
      kTag, "write speaker frame");
  return bytes_written == sizeof(raw) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t AudioHal::set_amplifier_enabled(bool /*enabled*/) {
  // Hat SPK2 exposes BCLK/LRCK/SDATA only. Keep this lifecycle hook so the
  // voice controller need not special-case the old bare amplifier, but never
  // drive a separate enable GPIO because Hat SPK2 exposes no such pin.
  return ESP_OK;
}

}  // namespace sesame::audio
