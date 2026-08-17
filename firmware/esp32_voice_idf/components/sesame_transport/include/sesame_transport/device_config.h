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
  std::array<char, 257> device_token{};
  std::array<char, 4097> root_ca{};
  // Optional mDNS compatibility alias for the local HTTP control page. The
  // device ID remains the primary unique hostname; an alias is opt-in because
  // a generic name would collide when multiple robots share one Wi-Fi.
  std::array<char, 64> web_control_hostname{};
  std::array<char, 101> conversation_id{};

  DeviceConfig view() const;
};

esp_err_t load_device_config(StoredDeviceConfig* output);
esp_err_t save_conversation_id(const char* conversation_id);
esp_err_t load_wake_threshold_hundredths(uint8_t* output);
esp_err_t save_wake_threshold_hundredths(uint8_t value);

}  // namespace sesame::transport
