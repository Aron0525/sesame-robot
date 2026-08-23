#include "sesame_transport/gateway_client.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mdns.h"

#include "sesame_transport/transport_policy.h"

namespace sesame::transport {
namespace {

constexpr char kTag[] = "sesame_transport";
constexpr EventBits_t kWifiConnected = BIT0;

const char* txt_value(const mdns_result_t* result, const char* key) {
  for (size_t index = 0; index < result->txt_count; ++index) {
    if (std::strcmp(result->txt[index].key, key) == 0) {
      return result->txt[index].value;
    }
  }
  return "";
}

}  // namespace

GatewayClient::~GatewayClient() { stop(); }

esp_err_t GatewayClient::setup_wifi_events() {
  wifi_events_ = xEventGroupCreate();
  if (wifi_events_ == nullptr) return ESP_ERR_NO_MEM;

  const esp_timer_create_args_t timer_args{
      .callback = reconnect_timer_callback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "sesame_wifi",
      .skip_unhandled_events = false,
  };
  esp_err_t result = esp_timer_create(&timer_args, &reconnect_timer_);
  if (result != ESP_OK) {
    cleanup_wifi_events();
    return result;
  }
  result = esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, this, &wifi_event_instance_);
  if (result != ESP_OK) {
    cleanup_wifi_events();
    return result;
  }
  result = esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, this, &got_ip_event_instance_);
  if (result != ESP_OK) cleanup_wifi_events();
  return result;
}

void GatewayClient::cleanup_wifi_events() {
  if (wifi_event_instance_ != nullptr) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_instance_));
    wifi_event_instance_ = nullptr;
  }
  if (got_ip_event_instance_ != nullptr) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(
        IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_event_instance_));
    got_ip_event_instance_ = nullptr;
  }
  if (reconnect_timer_ != nullptr) {
    if (esp_timer_is_active(reconnect_timer_)) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(reconnect_timer_));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_delete(reconnect_timer_));
    reconnect_timer_ = nullptr;
  }
  if (wifi_events_ != nullptr) {
    vEventGroupDelete(wifi_events_);
    wifi_events_ = nullptr;
  }
  reconnect_scheduler_.on_got_ip();
}

void GatewayClient::wifi_event(void* handler_arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  auto* self = static_cast<GatewayClient*>(handler_arg);
  if (self != nullptr) self->handle_wifi_event(event_base, event_id, event_data);
}

void GatewayClient::handle_wifi_event(esp_event_base_t event_base,
                                      int32_t event_id,
                                      const void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_connected_.store(false);
    if (wifi_events_ != nullptr) xEventGroupClearBits(wifi_events_, kWifiConnected);
    const auto* disconnected =
        static_cast<const wifi_event_sta_disconnected_t*>(event_data);
    if (disconnected != nullptr) {
      ESP_LOGW(kTag, "Wi-Fi disconnected (reason=%u, rssi=%d)",
               static_cast<unsigned>(disconnected->reason),
               static_cast<int>(disconnected->rssi));
    } else {
      ESP_LOGW(kTag, "Wi-Fi disconnected");
    }
    if (!stopping_.load()) schedule_wifi_reconnect();
    return;
  }
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    wifi_connected_.store(true);
    reconnect_scheduler_.on_got_ip();
    if (reconnect_timer_ != nullptr && esp_timer_is_active(reconnect_timer_)) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(reconnect_timer_));
    }
    if (wifi_events_ != nullptr) xEventGroupSetBits(wifi_events_, kWifiConnected);
    ESP_LOGI(kTag, "Wi-Fi got IP; reconnect backoff reset");
  }
}

void GatewayClient::schedule_wifi_reconnect() {
  if (stopping_.load() || reconnect_timer_ == nullptr) return;
  const std::optional<uint32_t> delay_ms = reconnect_scheduler_.schedule();
  if (!delay_ms.has_value()) return;

  const esp_err_t result = esp_timer_start_once(
      reconnect_timer_, static_cast<uint64_t>(*delay_ms) * 1000);
  if (result != ESP_OK) {
    reconnect_scheduler_.on_timer_fired();
    ESP_LOGE(kTag, "schedule Wi-Fi reconnect failed: %s",
             esp_err_to_name(result));
    return;
  }
  ESP_LOGW(kTag, "retry Wi-Fi connection in %lu ms",
           static_cast<unsigned long>(*delay_ms));
}

void GatewayClient::reconnect_timer_callback(void* callback_arg) {
  auto* self = static_cast<GatewayClient*>(callback_arg);
  if (self != nullptr) self->reconnect_wifi();
}

void GatewayClient::reconnect_wifi() {
  reconnect_scheduler_.on_timer_fired();
  if (stopping_.load() || wifi_connected_.load()) return;

  const esp_err_t result = esp_wifi_connect();
  if (result == ESP_OK) {
    ESP_LOGI(kTag, "starting Wi-Fi reconnect attempt");
    return;
  }
  ESP_LOGW(kTag, "Wi-Fi reconnect attempt failed: %s", esp_err_to_name(result));
  schedule_wifi_reconnect();
}

esp_err_t GatewayClient::connect_wifi(const StoredDeviceConfig& config) {
  stopping_.store(false);
  wifi_connected_.store(false);
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_loop_create_default());
  if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
    esp_netif_create_default_wifi_sta();
  }

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t result = esp_wifi_init(&init_config);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;

  result = setup_wifi_events();
  if (result != ESP_OK) return result;

  wifi_config_t wifi_config{};
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid),
               config.wifi_ssid.data(), sizeof(wifi_config.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
               config.wifi_password.data(),
               sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result == ESP_OK) {
    result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  }
  if (result == ESP_OK) result = esp_wifi_start();
  if (result == ESP_OK) result = esp_wifi_connect();
  if (result == ESP_OK) {
    const EventBits_t bits = xEventGroupWaitBits(
        wifi_events_, kWifiConnected, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if ((bits & kWifiConnected) == 0) result = ESP_ERR_TIMEOUT;
  }

  if (result != ESP_OK) {
    stopping_.store(true);
    cleanup_wifi_events();
  }
  return result;
}

esp_err_t GatewayClient::discover_gateway(
    const StoredDeviceConfig& config) {
  esp_err_t result = mdns_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;

  mdns_result_t* results = nullptr;
  result = mdns_query_ptr("_sesame-gw", "_tcp", 5000, 8, &results);
  if (result != ESP_OK) return result;

  esp_err_t selected = ESP_ERR_NOT_FOUND;
  for (mdns_result_t* current = results; current != nullptr;
       current = current->next) {
    GatewayCandidate candidate{
        current->hostname == nullptr ? "" : current->hostname,
        current->port,
        txt_value(current, "gateway_id"),
        txt_value(current, "protocol"),
        txt_value(current, "tls"),
        txt_value(current, "path"),
    };
    if (validate_candidate(candidate, config.gateway_id.data()) !=
        CandidateError::kOk) {
      continue;
    }
    const int written = std::snprintf(
        uri_.data(), uri_.size(), "wss://%s:%u/v1/device-stream",
        current->hostname, current->port);
    if (written > 0 && static_cast<size_t>(written) < uri_.size()) {
      selected = ESP_OK;
      break;
    }
  }
  mdns_query_results_free(results);
  return selected;
}

esp_err_t GatewayClient::start(const StoredDeviceConfig& config,
                               GatewayObserver* observer) {
  if (observer == nullptr) return ESP_ERR_INVALID_ARG;
  if (validate_device_config(config.view()) != ConfigError::kOk) {
    return ESP_ERR_INVALID_ARG;
  }
  stop();
  observer_ = observer;

  esp_err_t result = connect_wifi(config);
  if (result != ESP_OK) {
    stop();
    return result;
  }
  const DeviceConfig config_view = config.view();
  if (config_view.gateway_url.empty()) {
    result = discover_gateway(config);
    if (result != ESP_OK) {
      stop();
      return result;
    }
  } else {
    const int written = std::snprintf(uri_.data(), uri_.size(), "%.*s",
                                      static_cast<int>(config_view.gateway_url.size()),
                                      config_view.gateway_url.data());
    if (written <= 0 || static_cast<size_t>(written) >= uri_.size()) {
      stop();
      return ESP_ERR_INVALID_SIZE;
    }
  }

  const int header_size = std::snprintf(
      authorization_.data(), authorization_.size(),
      "Authorization: Bearer %s\r\n", config.device_token.data());
  if (header_size <= 0 ||
      static_cast<size_t>(header_size) >= authorization_.size()) {
    stop();
    return ESP_ERR_INVALID_SIZE;
  }

  const esp_websocket_client_config_t websocket_config{
      .uri = uri_.data(),
      .disable_auto_reconnect = false,
      .enable_close_reconnect = true,
      .user_context = this,
      .task_prio = 6,
      .task_name = "sesame_wss",
      .task_stack = 8192,
      .buffer_size = 2048,
      .cert_pem = config.root_ca.data(),
      .headers = authorization_.data(),
      .pingpong_timeout_sec = 15,
      .disable_pingpong_discon = false,
      .keep_alive_enable = true,
      .keep_alive_idle = 5,
      .keep_alive_interval = 5,
      .keep_alive_count = 3,
      .reconnect_timeout_ms = 1000,
      .network_timeout_ms = 10000,
      .ping_interval_sec = 10,
  };
  client_ = esp_websocket_client_init(&websocket_config);
  if (client_ == nullptr) {
    stop();
    return ESP_ERR_NO_MEM;
  }
  result = esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY,
                                         websocket_event, this);
  if (result == ESP_OK) result = esp_websocket_client_start(client_);
  if (result != ESP_OK) stop();
  return result;
}

void GatewayClient::stop() {
  stopping_.store(true);
  wifi_connected_.store(false);
  if (client_ != nullptr) {
    esp_websocket_client_stop(client_);
    esp_websocket_client_destroy(client_);
    client_ = nullptr;
  }
  cleanup_wifi_events();
  observer_ = nullptr;
  receive_size_ = 0;
}

bool GatewayClient::connected() const {
  return client_ != nullptr && esp_websocket_client_is_connected(client_);
}

esp_err_t GatewayClient::send_text(const char* data, size_t size) {
  if (!connected()) return ESP_ERR_INVALID_STATE;
  if (data == nullptr || size == 0 || size > 16384) return ESP_ERR_INVALID_ARG;
  const int sent = esp_websocket_client_send_text(
      client_, data, static_cast<int>(size), pdMS_TO_TICKS(2000));
  return sent == static_cast<int>(size) ? ESP_OK : ESP_FAIL;
}

esp_err_t GatewayClient::send_binary(const uint8_t* data, size_t size) {
  if (!connected()) return ESP_ERR_INVALID_STATE;
  if (data == nullptr || size == 0 || size > receive_buffer_.size()) {
    return ESP_ERR_INVALID_ARG;
  }
  const int sent = esp_websocket_client_send_bin(
      client_, reinterpret_cast<const char*>(data), static_cast<int>(size),
      pdMS_TO_TICKS(2000));
  return sent == static_cast<int>(size) ? ESP_OK : ESP_FAIL;
}

void GatewayClient::websocket_event(void* handler_arg, esp_event_base_t,
                                    int32_t event_id, void* event_data) {
  auto* self = static_cast<GatewayClient*>(handler_arg);
  if (self == nullptr || event_data == nullptr) return;
  self->handle_websocket_event(
      event_id, *static_cast<esp_websocket_event_data_t*>(event_data));
}

void GatewayClient::handle_websocket_event(
    int32_t event_id, const esp_websocket_event_data_t& event) {
  if (observer_ == nullptr) return;
  if (event_id == WEBSOCKET_EVENT_CONNECTED) {
    receive_size_ = 0;
    observer_->on_gateway_connected();
    return;
  }
  if (event_id == WEBSOCKET_EVENT_DISCONNECTED ||
      event_id == WEBSOCKET_EVENT_CLOSED) {
    receive_size_ = 0;
    observer_->on_gateway_disconnected();
    return;
  }
  if (event_id != WEBSOCKET_EVENT_DATA || event.data_len < 0 ||
      event.payload_len <= 0 ||
      static_cast<size_t>(event.payload_len) > receive_buffer_.size()) {
    return;
  }
  if (event.payload_offset == 0) {
    receive_size_ = 0;
    receive_opcode_ = event.op_code;
  }
  if (event.payload_offset != static_cast<int>(receive_size_) ||
      receive_size_ + event.data_len > receive_buffer_.size()) {
    receive_size_ = 0;
    return;
  }
  std::memcpy(receive_buffer_.data() + receive_size_, event.data_ptr,
              event.data_len);
  receive_size_ += event.data_len;
  if (!event.fin || receive_size_ != static_cast<size_t>(event.payload_len)) {
    return;
  }
  if (receive_opcode_ == 0x1) {
    if (receive_size_ < receive_buffer_.size()) {
      receive_buffer_[receive_size_] = '\0';
      observer_->on_gateway_text(
          reinterpret_cast<const char*>(receive_buffer_.data()),
          receive_size_);
    }
  } else if (receive_opcode_ == 0x2) {
    observer_->on_gateway_binary(receive_buffer_.data(), receive_size_);
  }
  receive_size_ = 0;
}

}  // namespace sesame::transport
