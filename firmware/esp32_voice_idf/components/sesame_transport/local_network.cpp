#include "sesame_transport/local_network.h"

#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "mdns.h"

#include "sesame_transport/transport_policy.h"

namespace sesame::transport {
namespace {

constexpr char kTag[] = "sesame_network";
constexpr char kHttpService[] = "_http";
constexpr char kTcpProtocol[] = "_tcp";

}  // namespace

LocalNetwork::~LocalNetwork() {
  if (!event_handlers_registered_) return;
  esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler_);
  esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                        ip_event_handler_);
}

esp_err_t LocalNetwork::start(const LocalNetworkConfig& config) {
  if (started_) return ESP_OK;
  esp_err_t result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
  result = esp_event_loop_create_default();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;

  station_ = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (station_ == nullptr) station_ = esp_netif_create_default_wifi_sta();
  if (station_ == nullptr) return ESP_ERR_NO_MEM;

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  result = esp_wifi_init(&init_config);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;

  if (!event_handlers_registered_) {
    result = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, LocalNetwork::wifi_event, this,
        &wifi_event_handler_);
    if (result != ESP_OK) return result;
    result = esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, LocalNetwork::wifi_event, this,
        &ip_event_handler_);
    if (result != ESP_OK) return result;
    event_handlers_registered_ = true;
  }

  station_enabled_ = validate_local_network_config(config);
  if (!station_enabled_) return ESP_ERR_INVALID_ARG;
  result = esp_wifi_set_mode(WIFI_MODE_STA);
  if (result != ESP_OK && result != ESP_ERR_WIFI_STATE) return result;
  wifi_config_t station_config{};
  std::strncpy(reinterpret_cast<char*>(station_config.sta.ssid),
               config.wifi_ssid.data(), sizeof(station_config.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(station_config.sta.password),
               config.wifi_password.data(),
               sizeof(station_config.sta.password) - 1);
  station_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  station_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  station_config.sta.pmf_cfg.capable = true;
  station_config.sta.pmf_cfg.required = false;
  station_config.sta.failure_retry_cnt = 3;
  result = esp_wifi_set_config(WIFI_IF_STA, &station_config);
  if (result != ESP_OK && result != ESP_ERR_WIFI_STATE) return result;

  result = esp_wifi_start();
  if (result != ESP_OK && result != ESP_ERR_WIFI_STATE) return result;
  started_ = true;
  ESP_LOGI(kTag, "STA startup ready; awaiting DHCP address");

  const esp_err_t mdns_result = start_mdns(config);
  if (mdns_result != ESP_OK) {
    ESP_LOGW(kTag, "mDNS unavailable; direct-IP control remains available: %s",
             esp_err_to_name(mdns_result));
  }
  request_station_connection();
  return ESP_OK;
}

void LocalNetwork::wifi_event(void* context, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
  auto* self = static_cast<LocalNetwork*>(context);
  if (self == nullptr) return;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    self->station_connection_requested_.store(false);
    self->request_station_connection();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    self->station_connection_requested_.store(false);
    self->request_station_connection();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const auto* got_ip = static_cast<const ip_event_got_ip_t*>(event_data);
    if (got_ip != nullptr) {
      ESP_LOGI(kTag, "STA connected: address=" IPSTR, IP2STR(&got_ip->ip_info.ip));
    }
  }
}

void LocalNetwork::request_station_connection() {
  if (!station_enabled_ || station_connection_requested_.exchange(true)) return;
  const esp_err_t result = esp_wifi_connect();
  if (result != ESP_OK && result != ESP_ERR_WIFI_CONN &&
      result != ESP_ERR_WIFI_NOT_STARTED) {
    station_connection_requested_.store(false);
    ESP_LOGW(kTag, "STA reconnect request failed: %s", esp_err_to_name(result));
  }
}

esp_err_t LocalNetwork::start_mdns(const LocalNetworkConfig& config) {
  esp_err_t result = mdns_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;

  char hostname[64]{};
  const std::string_view requested_hostname =
      config.web_control_hostname.empty() ? config.device_id
                                          : config.web_control_hostname;
  if (!format_device_mdns_hostname(requested_hostname, hostname,
                                   sizeof(hostname))) {
    std::strncpy(hostname, "sesame-robot", sizeof(hostname) - 1);
  }
  result = mdns_hostname_set(hostname);
  if (result != ESP_OK) return result;
  const auto action = static_cast<mdns_event_actions_t>(
      MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4);
  result = mdns_netif_action(station_, action);
  if (result != ESP_OK) return result;
  if (!mdns_service_exists(kHttpService, kTcpProtocol, nullptr)) {
    result = mdns_service_add(hostname, kHttpService, kTcpProtocol, 80, nullptr, 0);
    if (result != ESP_OK) return result;
  }
  ESP_LOGI(kTag, "local control announced: http://%s.local/", hostname);
  return ESP_OK;
}

}  // namespace sesame::transport
