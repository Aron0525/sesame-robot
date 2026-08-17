#include "sesame_transport/device_config.h"

#include <cstring>
#include <string_view>

#include "nvs.h"
#include "nvs_flash.h"

#if __has_include("sesame_transport/local-device-config.h")
#include "sesame_transport/local-device-config.h"
#define SESAME_HAS_LOCAL_DEVICE_CONFIG 1
#else
#define SESAME_HAS_LOCAL_DEVICE_CONFIG 0
#endif

namespace sesame::transport {
namespace {

constexpr uint8_t kDefaultWakeThresholdHundredths = 20;
constexpr uint8_t kMinimumWakeThresholdHundredths = 5;
constexpr uint8_t kMaximumWakeThresholdHundredths = 95;

bool is_valid_wake_threshold_hundredths(uint8_t value) {
  return value >= kMinimumWakeThresholdHundredths &&
         value <= kMaximumWakeThresholdHundredths;
}

esp_err_t ensure_nvs_initialized() {
  const esp_err_t result = nvs_flash_init();
  return result == ESP_ERR_INVALID_STATE ? ESP_OK : result;
}

template <size_t Capacity>
esp_err_t read_string(nvs_handle_t handle, const char* key,
                      std::array<char, Capacity>* output, bool required) {
  size_t length = output->size();
  const esp_err_t result =
      nvs_get_str(handle, key, output->data(), &length);
  if (result == ESP_ERR_NVS_NOT_FOUND && !required) {
    output->front() = '\0';
    return ESP_OK;
  }
  if (result != ESP_OK || length == 0 || length > output->size()) {
    output->front() = '\0';
    return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
  }
  output->back() = '\0';
  return ESP_OK;
}

#if SESAME_HAS_LOCAL_DEVICE_CONFIG
#if !defined(SESAME_LOCAL_WIFI_SSID) || !defined(SESAME_LOCAL_WIFI_PASSWORD) || \
    !defined(SESAME_LOCAL_DEVICE_ID) || !defined(SESAME_LOCAL_GATEWAY_ID) || \
    !defined(SESAME_LOCAL_DEVICE_TOKEN) || !defined(SESAME_LOCAL_ROOT_CA)
#error "local-device-config.h must define every SESAME_LOCAL_* value"
#endif

#if !defined(SESAME_LOCAL_WEB_CONTROL_HOSTNAME)
#define SESAME_LOCAL_WEB_CONTROL_HOSTNAME ""
#endif

template <size_t Capacity>
bool copy_local_value(std::string_view value,
                      std::array<char, Capacity>* output) {
  if (output == nullptr || value.empty() || value.size() >= output->size()) {
    if (output != nullptr) output->fill('\0');
    return false;
  }
  output->fill('\0');
  std::memcpy(output->data(), value.data(), value.size());
  return true;
}

esp_err_t load_local_device_config(StoredDeviceConfig* output) {
  const bool copied =
      copy_local_value(SESAME_LOCAL_WIFI_SSID, &output->wifi_ssid) &&
      copy_local_value(SESAME_LOCAL_WIFI_PASSWORD, &output->wifi_password) &&
      copy_local_value(SESAME_LOCAL_DEVICE_ID, &output->device_id) &&
      copy_local_value(SESAME_LOCAL_GATEWAY_ID, &output->gateway_id) &&
      copy_local_value(SESAME_LOCAL_DEVICE_TOKEN, &output->device_token) &&
      copy_local_value(SESAME_LOCAL_ROOT_CA, &output->root_ca);
  if (copied && !std::string_view(SESAME_LOCAL_WEB_CONTROL_HOSTNAME).empty() &&
      !copy_local_value(SESAME_LOCAL_WEB_CONTROL_HOSTNAME,
                        &output->web_control_hostname)) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!copied || validate_device_config(output->view()) != ConfigError::kOk) {
    return ESP_ERR_INVALID_ARG;
  }
  return ESP_OK;
}
#endif

}  // namespace

DeviceConfig StoredDeviceConfig::view() const {
  return {
      wifi_ssid.data(), wifi_password.data(), device_id.data(),
      gateway_id.data(), device_token.data(), root_ca.data(),
  };
}

esp_err_t load_device_config(StoredDeviceConfig* output) {
  if (output == nullptr) return ESP_ERR_INVALID_ARG;
  *output = {};

#if SESAME_HAS_LOCAL_DEVICE_CONFIG
  return load_local_device_config(output);
#else
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    return result;
  }
  if (result != ESP_OK) return result;

  nvs_handle_t handle = 0;
  result = nvs_open("sesame", NVS_READONLY, &handle);
  if (result != ESP_OK) return result;

  result = read_string(handle, "wifi_ssid", &output->wifi_ssid, true);
  if (result == ESP_OK) {
    result = read_string(handle, "wifi_pass", &output->wifi_password, true);
  }
  if (result == ESP_OK) {
    result = read_string(handle, "device_id", &output->device_id, true);
  }
  if (result == ESP_OK) {
    result = read_string(handle, "gateway_id", &output->gateway_id, true);
  }
  if (result == ESP_OK) {
    result = read_string(handle, "device_token", &output->device_token, true);
  }
  if (result == ESP_OK) {
    result = read_string(handle, "root_ca", &output->root_ca, true);
  }
  if (result == ESP_OK) {
    result = read_string(handle, "web_host", &output->web_control_hostname,
                         false);
  }
  if (result == ESP_OK) {
    result =
        read_string(handle, "conversation", &output->conversation_id, false);
  }
  nvs_close(handle);

  if (result != ESP_OK) return result;
  return validate_device_config(output->view()) == ConfigError::kOk
             ? ESP_OK
             : ESP_ERR_INVALID_ARG;
#endif
}

esp_err_t save_conversation_id(const char* conversation_id) {
  if (conversation_id == nullptr ||
      strnlen(conversation_id, 101) > 100) {
    return ESP_ERR_INVALID_ARG;
  }
#if SESAME_HAS_LOCAL_DEVICE_CONFIG
  // Compile-time configuration deliberately avoids NVS writes. The current
  // conversation remains valid for this boot and is reissued after reboot.
  return ESP_OK;
#else
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open("sesame", NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;
  result = nvs_set_str(handle, "conversation", conversation_id);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result;
#endif
}

esp_err_t load_wake_threshold_hundredths(uint8_t* output) {
  if (output == nullptr) return ESP_ERR_INVALID_ARG;
  *output = kDefaultWakeThresholdHundredths;
  const esp_err_t init_result = ensure_nvs_initialized();
  if (init_result != ESP_OK) return init_result;

  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open("sesame", NVS_READONLY, &handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (result != ESP_OK) return result;

  uint8_t saved_value = kDefaultWakeThresholdHundredths;
  result = nvs_get_u8(handle, "wake_threshold", &saved_value);
  nvs_close(handle);
  if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (result != ESP_OK) return result;
  if (!is_valid_wake_threshold_hundredths(saved_value)) return ESP_OK;
  *output = saved_value;
  return ESP_OK;
}

esp_err_t save_wake_threshold_hundredths(uint8_t value) {
  if (!is_valid_wake_threshold_hundredths(value)) return ESP_ERR_INVALID_ARG;
  const esp_err_t init_result = ensure_nvs_initialized();
  if (init_result != ESP_OK) return init_result;

  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open("sesame", NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;
  result = nvs_set_u8(handle, "wake_threshold", value);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result;
}

}  // namespace sesame::transport
