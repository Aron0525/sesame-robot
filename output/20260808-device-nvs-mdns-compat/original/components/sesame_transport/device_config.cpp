#include "sesame_transport/device_config.h"

#include <cstring>

#include "nvs.h"
#include "nvs_flash.h"

namespace sesame::transport {
namespace {

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

}  // namespace

DeviceConfig StoredDeviceConfig::view() const {
  return {
      wifi_ssid.data(), wifi_password.data(), device_id.data(),
      gateway_id.data(), gateway_url.data(), device_token.data(), root_ca.data(),
  };
}

esp_err_t load_device_config(StoredDeviceConfig* output) {
  if (output == nullptr) return ESP_ERR_INVALID_ARG;
  *output = {};

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
    result = read_string(handle, "gateway_url", &output->gateway_url, true);
  }
  if (result == ESP_OK) {
    result = read_string(handle, "device_token", &output->device_token, true);
  }
  if (result == ESP_OK) {
    result = read_string(handle, "root_ca", &output->root_ca, true);
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
}

esp_err_t save_conversation_id(const char* conversation_id) {
  if (conversation_id == nullptr ||
      strnlen(conversation_id, 101) > 100) {
    return ESP_ERR_INVALID_ARG;
  }
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open("sesame", NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;
  result = nvs_set_str(handle, "conversation", conversation_id);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result;
}

}  // namespace sesame::transport
