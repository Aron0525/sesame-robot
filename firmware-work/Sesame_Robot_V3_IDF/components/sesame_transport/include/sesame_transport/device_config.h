#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"

#include "sesame_transport/transport_policy.h"

namespace sesame::transport {

struct StoredDeviceConfig {
  std::array<char, 33> wifi_ssid{};
  std::array<char, 65> wifi_password{};
  std::array<char, 101> device_id{};
  std::array<char, 101> gateway_id{};
  std::array<char, 257> gateway_url{};
  std::array<char, 257> gateway_tls_name{};
  std::array<char, 257> device_token{};
  std::array<char, 4097> root_ca{};
  std::array<char, 101> conversation_id{};

  DeviceConfig view() const;
};

esp_err_t load_device_config(StoredDeviceConfig* output);
esp_err_t save_conversation_id(const char* conversation_id);
esp_err_t load_wake_threshold_hundredths(uint8_t* output);
esp_err_t save_wake_threshold_hundredths(uint8_t value);

}  // namespace sesame::transport
