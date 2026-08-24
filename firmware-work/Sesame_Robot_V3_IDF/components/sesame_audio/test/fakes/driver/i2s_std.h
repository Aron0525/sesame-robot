#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "driver/gpio.h"

struct FakeI2sChannel;
using i2s_chan_handle_t = FakeI2sChannel*;
using i2s_port_t = int;

inline constexpr int I2S_ROLE_MASTER = 1;
inline constexpr int I2S_DATA_BIT_WIDTH_32BIT = 32;
inline constexpr int I2S_SLOT_MODE_STEREO = 2;
inline constexpr gpio_num_t I2S_GPIO_UNUSED = -1;

struct i2s_chan_config_t {
  i2s_port_t id;
  int role;
  int dma_desc_num;
  int dma_frame_num;
  bool auto_clear;
  int intr_priority;
};

#define I2S_CHANNEL_DEFAULT_CONFIG(port, channel_role) \
  i2s_chan_config_t {                                    \
    .id = (port), .role = (channel_role), .dma_desc_num = 6, \
    .dma_frame_num = 240, .auto_clear = false, .intr_priority = 0 \
  }

struct i2s_std_clk_config_t {
  uint32_t sample_rate_hz;
};

struct i2s_std_slot_config_t {
  int data_bit_width;
  int slot_mode;
};

struct i2s_std_gpio_config_t {
  gpio_num_t mclk;
  gpio_num_t bclk;
  gpio_num_t ws;
  gpio_num_t dout;
  gpio_num_t din;
  struct {
    bool mclk_inv;
    bool bclk_inv;
    bool ws_inv;
  } invert_flags;
};

struct i2s_std_config_t {
  i2s_std_clk_config_t clk_cfg;
  i2s_std_slot_config_t slot_cfg;
  i2s_std_gpio_config_t gpio_cfg;
};

#define I2S_STD_CLK_DEFAULT_CONFIG(rate) \
  i2s_std_clk_config_t { .sample_rate_hz = (rate) }
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, mode) \
  i2s_std_slot_config_t { .data_bit_width = (width), .slot_mode = (mode) }

esp_err_t i2s_new_channel(const i2s_chan_config_t* config,
                          i2s_chan_handle_t* tx,
                          i2s_chan_handle_t* rx);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel,
                                    const i2s_std_config_t* config);
esp_err_t i2s_channel_enable(i2s_chan_handle_t channel);
esp_err_t i2s_channel_disable(i2s_chan_handle_t channel);
esp_err_t i2s_del_channel(i2s_chan_handle_t channel);
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t channel,
                                   const void* source,
                                   size_t size,
                                   size_t* bytes_loaded);
esp_err_t i2s_channel_write(i2s_chan_handle_t channel,
                            const void* source,
                            size_t size,
                            size_t* bytes_written,
                            uint32_t timeout_ticks);
esp_err_t i2s_channel_read(i2s_chan_handle_t channel,
                           void* destination,
                           size_t size,
                           size_t* bytes_read,
                           uint32_t timeout_ticks);
