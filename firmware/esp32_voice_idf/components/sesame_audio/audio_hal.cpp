#include "sesame_audio/audio_hal.h"

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
      I2S_CHANNEL_DEFAULT_CONFIG(
          static_cast<i2s_port_t>(kMicrophoneI2sPort), I2S_ROLE_MASTER);
  ESP_RETURN_ON_ERROR(
      i2s_new_channel(&microphone_channel_config, nullptr, &rx_channel_), kTag,
      "create microphone I2S RX channel");

  const i2s_chan_config_t speaker_channel_config =
      I2S_CHANNEL_DEFAULT_CONFIG(
          static_cast<i2s_port_t>(kSpeakerI2sPort), I2S_ROLE_MASTER);
  esp_err_t result = i2s_new_channel(&speaker_channel_config, &tx_channel_, nullptr);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }

  i2s_std_config_t tx_config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
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
  result = i2s_channel_init_std_mode(rx_channel_, &rx_config);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }

  result = i2s_channel_enable(tx_channel_);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }
  result = i2s_channel_enable(rx_channel_);
  if (result != ESP_OK) {
    shutdown();
    return result;
  }

  initialized_ = true;
  ESP_LOGI(kTag,
           "I2S ready: mic GPIO 14/47/48 on I2S0; speaker GPIO 1/2/3 on I2S1");
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

  std::array<int32_t, kRawSamplesPerFrame> raw{};
  for (size_t frame = 0; frame < kSamplesPerFrame; ++frame) {
    const int32_t expanded = static_cast<int32_t>(input[frame]) << 16;
    raw[frame * kI2sSlotsPerFrame] = expanded;
    raw[frame * kI2sSlotsPerFrame + 1] = expanded;
  }

  size_t bytes_written = 0;
  ESP_RETURN_ON_ERROR(
      i2s_channel_write(tx_channel_, raw.data(), sizeof(raw), &bytes_written,
                        pdMS_TO_TICKS(timeout_ms)),
      kTag, "write speaker frame");
  return bytes_written == sizeof(raw) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t AudioHal::set_amplifier_enabled(bool enabled) {
  // GPIO 1 is the replacement MAX98357A BCLK, not a shutdown/enable line.
  // Keep the existing voice-controller API while the always-enabled amplifier
  // is driven solely by its I2S stream.
  (void)enabled;
  return ESP_OK;
}

}  // namespace sesame::audio
