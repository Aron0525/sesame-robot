#include "sesame_transport/gateway_client.h"

#include <algorithm>
#include <ctime>
#include <sys/time.h>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#include "mdns.h"

#include "sesame_transport/transport_policy.h"

namespace sesame::transport {
namespace {

constexpr char kTag[] = "sesame_transport";
constexpr char kWebControlService[] = "_http";
constexpr char kTcpProtocol[] = "_tcp";
constexpr EventBits_t kWifiConnected = BIT0;
constexpr TickType_t kWifiConnectWaitTicks = pdMS_TO_TICKS(20000);

const char* txt_value(const mdns_result_t* result, const char* key) {
  for (size_t index = 0; index < result->txt_count; ++index) {
    if (std::strcmp(result->txt[index].key, key) == 0) {
      return result->txt[index].value;
    }
  }
  return "";
}

bool station_has_ipv4(esp_netif_t* station) {
  if (station == nullptr) return false;
  esp_netif_ip_info_t ip_info{};
  return esp_netif_get_ip_info(station, &ip_info) == ESP_OK &&
         ip_info.ip.addr != 0;
}

bool format_mdns_ipv4(const mdns_result_t* result, char* output,
                      size_t capacity) {
  if (result == nullptr || output == nullptr || capacity == 0) return false;
  for (const mdns_ip_addr_t* current = result->addr; current != nullptr;
       current = current->next) {
    if (!IP_IS_V4(&current->addr)) continue;
    return esp_ip4addr_ntoa(&current->addr.u_addr.ip4, output, capacity) !=
           nullptr;
  }
  return false;
}

}  // namespace

GatewayClient::~GatewayClient() {
  stop();
  if (sntp_initialized_) {
    esp_netif_sntp_deinit();
    sntp_initialized_ = false;
  }
  if (client_mutex_ != nullptr) {
    vSemaphoreDelete(client_mutex_);
    client_mutex_ = nullptr;
  }
  if (wifi_handlers_registered_) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_event_handler_);
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                          ip_event_handler_);
    wifi_handlers_registered_ = false;
  }
  if (wifi_event_group_ != nullptr) {
    vEventGroupDelete(wifi_event_group_);
    wifi_event_group_ = nullptr;
  }
}

void GatewayClient::wifi_event(void* context, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  auto* self = static_cast<GatewayClient*>(context);
  if (self == nullptr || self->wifi_event_group_ == nullptr) return;

  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_STA_START) {
      xEventGroupClearBits(self->wifi_event_group_, kWifiConnected);
      self->wifi_connection_requested_.store(false);
      self->request_wifi_connection();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      xEventGroupClearBits(self->wifi_event_group_, kWifiConnected);
      self->wifi_connection_requested_.store(false);
      const auto* disconnected =
          static_cast<const wifi_event_sta_disconnected_t*>(event_data);
      const uint8_t diagnostic_index =
          self->wifi_disconnect_diagnostic_count_.fetch_add(1);
      if (diagnostic_index < 3) {
        ESP_LOGW(kTag, "Wi-Fi STA disconnected: reason=%u",
                 disconnected == nullptr
                     ? 0U
                     : static_cast<unsigned>(disconnected->reason));
      }
      self->request_wifi_connection();
    }
    return;
  }
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    self->wifi_connection_requested_.store(false);
    xEventGroupSetBits(self->wifi_event_group_, kWifiConnected);
    const auto* got_ip = static_cast<const ip_event_got_ip_t*>(event_data);
    self->publish_web_control_alias(got_ip == nullptr ? nullptr : got_ip->esp_netif);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
    xEventGroupClearBits(self->wifi_event_group_, kWifiConnected);
    self->wifi_connection_requested_.store(false);
    self->clear_web_control_alias_address();
    self->request_wifi_connection();
  }
}

void GatewayClient::request_wifi_connection() {
  // WIFI_EVENT_STA_START and a caller returning from esp_wifi_start() can be
  // delivered very close together. Only the first path may issue a connect.
  if (wifi_connection_requested_.exchange(true)) return;

  const esp_err_t result = esp_wifi_connect();
  if (result == ESP_OK || result == ESP_ERR_WIFI_CONN) {
    // ESP-IDF reports ESP_ERR_WIFI_CONN when a prior association is still in
    // flight. Keep the gate closed and let the total connection timeout reset
    // that state if it never reaches GOT_IP.
    return;
  }
  wifi_connection_requested_.store(false);
  if (result != ESP_ERR_WIFI_STATE && result != ESP_ERR_WIFI_NOT_STARTED) {
    ESP_LOGW(kTag, "Wi-Fi reconnect request failed: %s", esp_err_to_name(result));
  }
}

void GatewayClient::reset_wifi_association_after_timeout() {
  // A driver can remain in association without emitting a disconnect event.
  // Cancel that attempt so its event handler starts a clean one on the next
  // reconnect cycle instead of repeatedly observing ESP_ERR_WIFI_CONN.
  wifi_connection_requested_.store(false);
  const esp_err_t result = esp_wifi_disconnect();
  if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED &&
      result != ESP_ERR_WIFI_NOT_INIT && result != ESP_ERR_WIFI_NOT_CONNECT) {
    ESP_LOGW(kTag, "Wi-Fi association reset failed: %s", esp_err_to_name(result));
  }
}

void GatewayClient::log_wifi_diagnostics_after_timeout(
    const StoredDeviceConfig& config) {
  if (wifi_timeout_scan_attempted_) return;
  wifi_timeout_scan_attempted_ = true;

  // Passive scan prevents this diagnostic from broadcasting a probe for the
  // configured network. We only emit coarse radio metadata, never its name.
  wifi_scan_config_t scan_config{};
  scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
  scan_config.scan_time.passive = 100;
  const esp_err_t result = esp_wifi_scan_start(&scan_config, true);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "Wi-Fi diagnostic scan unavailable: %s",
             esp_err_to_name(result));
    return;
  }

  uint16_t access_point_count = 0;
  if (esp_wifi_scan_get_ap_num(&access_point_count) != ESP_OK) {
    ESP_LOGW(kTag, "Wi-Fi diagnostic scan results unavailable");
    return;
  }

  std::array<wifi_ap_record_t, 20> records{};
  uint16_t record_count = std::min<uint16_t>(access_point_count, records.size());
  if (record_count > 0 &&
      esp_wifi_scan_get_ap_records(&record_count, records.data()) != ESP_OK) {
    ESP_LOGW(kTag, "Wi-Fi diagnostic scan records unavailable");
    return;
  }

  const size_t configured_length =
      strnlen(config.wifi_ssid.data(), config.wifi_ssid.size());
  for (uint16_t index = 0; index < record_count; ++index) {
    const wifi_ap_record_t& record = records[index];
    const size_t discovered_length = strnlen(
        reinterpret_cast<const char*>(record.ssid), sizeof(record.ssid));
    if (configured_length == 0 || discovered_length != configured_length ||
        std::memcmp(record.ssid, config.wifi_ssid.data(), configured_length) !=
            0) {
      continue;
    }
    ESP_LOGI(kTag,
             "Wi-Fi diagnostic: configured_network_seen=1 authmode=%u rssi=%d",
             static_cast<unsigned>(record.authmode), record.rssi);
    return;
  }
  ESP_LOGW(kTag, "Wi-Fi diagnostic: configured_network_seen=0");
}

esp_err_t GatewayClient::initialize_local_mdns(
    const StoredDeviceConfig& config, esp_netif_t* station) {
  if (station == nullptr) return ESP_ERR_INVALID_ARG;

  const esp_err_t init_result = mdns_init();
  if (init_result != ESP_OK && init_result != ESP_ERR_INVALID_STATE) {
    return init_result;
  }
  if (local_mdns_published_) return ESP_OK;

  std::array<char, 64> device_hostname{};
  if (!format_device_mdns_hostname(config.device_id.data(),
                                   device_hostname.data(),
                                   device_hostname.size())) {
    ESP_LOGW(kTag, "device_id cannot be used as an mDNS hostname");
    return ESP_ERR_INVALID_ARG;
  }

  web_control_hostname_.fill('\0');
  if (config.web_control_hostname.front() != '\0' &&
      !format_device_mdns_hostname(config.web_control_hostname.data(),
                                   web_control_hostname_.data(),
                                   web_control_hostname_.size())) {
    ESP_LOGW(kTag, "web control mDNS alias is invalid");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t result = mdns_hostname_set(device_hostname.data());
  if (result != ESP_OK) return result;

  // mDNS is initialized before the STA starts so its built-in GOT_IP handler
  // can publish an address after every DHCP lease. Queue an explicit
  // announcement too: it covers the retry path where Wi-Fi already received
  // an address before this client was constructed.
  const auto announce_action = static_cast<mdns_event_actions_t>(
      MDNS_EVENT_ENABLE_IP4 | MDNS_EVENT_ANNOUNCE_IP4);
  result = mdns_netif_action(station, announce_action);
  if (result != ESP_OK) return result;

  if (!mdns_service_exists(kWebControlService, kTcpProtocol, nullptr)) {
    result = mdns_service_add(device_hostname.data(), kWebControlService, kTcpProtocol, 80,
                              nullptr, 0);
    if (result != ESP_OK) return result;
  }

  local_mdns_published_ = true;
  ESP_LOGI(kTag, "local web control published: http://%s.local/",
           device_hostname.data());
  return ESP_OK;
}

void GatewayClient::publish_web_control_alias(esp_netif_t* station) {
  if (station == nullptr || web_control_hostname_.front() == '\0') return;

  esp_netif_ip_info_t ip_info{};
  if (esp_netif_get_ip_info(station, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
    ESP_LOGW(kTag, "web control mDNS alias skipped: station has no IPv4 address");
    return;
  }

  mdns_ip_addr_t address{};
  address.addr.type = ESP_IPADDR_TYPE_V4;
  address.addr.u_addr.ip4 = ip_info.ip;
  address.next = nullptr;

  const esp_err_t result = web_control_alias_published_
                               ? mdns_delegate_hostname_set_address(
                                     web_control_hostname_.data(), &address)
                               : mdns_delegate_hostname_add(
                                     web_control_hostname_.data(), &address);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "web control mDNS alias update failed: %s",
             esp_err_to_name(result));
    return;
  }

  web_control_alias_published_ = true;
  ESP_LOGI(kTag, "legacy web control alias published: http://%s.local/",
           web_control_hostname_.data());
}

void GatewayClient::clear_web_control_alias_address() {
  if (!web_control_alias_published_ || web_control_hostname_.front() == '\0') return;
  const esp_err_t result =
      mdns_delegate_hostname_set_address(web_control_hostname_.data(), nullptr);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "web control mDNS alias clear failed: %s", esp_err_to_name(result));
  }
}

esp_err_t GatewayClient::connect_wifi(const StoredDeviceConfig& config) {
  (void)config;
  // Wi-Fi belongs to LocalNetwork, which starts before the web or voice
  // runtimes. Gateway recovery must never configure, stop, or otherwise make
  // local AP/HTTP availability depend on mDNS, TLS, or WSS success.
  esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  return station_has_ipv4(station) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t GatewayClient::discover_gateway(
    const StoredDeviceConfig& config) {
  discovered_gateway_time_ = 0;
  esp_err_t selected = ESP_ERR_NOT_FOUND;
  std::array<char, 16> gateway_ipv4{};
  constexpr std::array<const char*, 2> services{
      kGatewayMdnsService, kLegacyGatewayMdnsService};
  for (const char* service : services) {
    mdns_result_t* results = nullptr;
    const esp_err_t query_result =
        mdns_query_ptr(service, "_tcp", 5000, 8, &results);
    if (query_result != ESP_OK) continue;
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
      if (!format_mdns_ipv4(current, gateway_ipv4.data(),
                            gateway_ipv4.size())) {
        ESP_LOGW(kTag, "mDNS Gateway record has no IPv4 address");
        continue;
      }
      if (format_gateway_tls_hostname(candidate, gateway_tls_name_.data(),
                                      gateway_tls_name_.size()) &&
          format_gateway_ipv4_uri(candidate, gateway_ipv4.data(), uri_.data(),
                                  uri_.size())) {
        parse_gateway_unix_time(txt_value(current, "unix_time"),
                                &discovered_gateway_time_);
        ESP_LOGI(kTag,
                 "P1 mDNS selected: service=%s host=%.*s port=%u gateway_id=%.*s",
                 service, static_cast<int>(candidate.host.size()),
                 candidate.host.data(), static_cast<unsigned>(candidate.port),
                 static_cast<int>(candidate.gateway_id.size()),
                 candidate.gateway_id.data());
        selected = ESP_OK;
        break;
      }
    }
    mdns_query_results_free(results);
    if (selected == ESP_OK) break;
  }
  return selected;
}

esp_err_t GatewayClient::ensure_tls_clock() {
  std::time_t now = std::time(nullptr);
  if (tls_clock_is_plausible(static_cast<int64_t>(now))) return ESP_OK;

  if (!sntp_initialized_) {
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    const esp_err_t init_result = esp_netif_sntp_init(&config);
    if (init_result != ESP_OK && init_result != ESP_ERR_INVALID_STATE) {
      return init_result;
    }
    sntp_initialized_ = true;
  }

  ESP_LOGI(kTag, "waiting for SNTP before TLS certificate validation");
  const esp_err_t sync_result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
  now = std::time(nullptr);
  if (sync_result == ESP_OK &&
      tls_clock_is_plausible(static_cast<int64_t>(now))) {
    ESP_LOGI(kTag, "system time synchronized for TLS: epoch=%lld",
             static_cast<long long>(now));
    return ESP_OK;
  }

  if (tls_clock_is_plausible(discovered_gateway_time_)) {
    const timeval gateway_time{
        static_cast<time_t>(discovered_gateway_time_), 0};
    if (settimeofday(&gateway_time, nullptr) == 0) {
      ESP_LOGW(kTag,
               "SNTP unavailable; initialized TLS clock from mDNS: epoch=%lld",
               static_cast<long long>(discovered_gateway_time_));
      return ESP_OK;
    }
  }
  return sync_result == ESP_OK ? ESP_ERR_INVALID_STATE : sync_result;
}

esp_err_t GatewayClient::start(const StoredDeviceConfig& config,
                               GatewayObserver* observer) {
  if (observer == nullptr) return ESP_ERR_INVALID_ARG;
  if (validate_device_config(config.view()) != ConfigError::kOk) {
    return ESP_ERR_INVALID_ARG;
  }
  if (ensure_client_mutex() != ESP_OK ||
      xSemaphoreTake(client_mutex_, portMAX_DELAY) != pdTRUE) {
    return ESP_ERR_NO_MEM;
  }
  stop_locked();
  observer_.store(observer);
  disconnect_reported_.store(false);

  esp_err_t result = connect_wifi(config);
  if (result != ESP_OK) {
    stop_locked();
    xSemaphoreGive(client_mutex_);
    return result;
  }
  // VoiceController owns exponential retry so every retry starts with a new
  // mDNS query instead of blocking this task through nested retry loops.
  result = discover_gateway(config);
  if (result != ESP_OK) {
    stop_locked();
    xSemaphoreGive(client_mutex_);
    return result;
  }
  result = ensure_tls_clock();
  if (result != ESP_OK) {
    stop_locked();
    xSemaphoreGive(client_mutex_);
    return result;
  }

  const int header_size = std::snprintf(
      authorization_.data(), authorization_.size(),
      "Authorization: Bearer %s\r\n", config.device_token.data());
  if (header_size <= 0 ||
      static_cast<size_t>(header_size) >= authorization_.size()) {
    stop_locked();
    xSemaphoreGive(client_mutex_);
    return ESP_ERR_INVALID_SIZE;
  }

  esp_websocket_client_config_t websocket_config{};
  websocket_config.uri = uri_.data();
  // Reconnect from VoiceController so the endpoint is rediscovered after
  // DHCP, mDNS service or Gateway-port changes.
  websocket_config.disable_auto_reconnect = true;
  websocket_config.enable_close_reconnect = false;
  websocket_config.user_context = this;
  websocket_config.task_prio = 6;
  websocket_config.task_name = "sesame_wss";
  websocket_config.task_stack = kWebsocketTaskStackBytes;
  websocket_config.buffer_size = 2048;
  websocket_config.cert_pem = config.root_ca.data();
  websocket_config.headers = authorization_.data();
  websocket_config.pingpong_timeout_sec = 15;
  websocket_config.disable_pingpong_discon = false;
  // TCP connects to the IPv4 address returned by the trusted mDNS query;
  // certificate validation remains bound to the advertised .local name.
  websocket_config.cert_common_name = gateway_tls_name_.data();
  websocket_config.keep_alive_enable = true;
  websocket_config.keep_alive_idle = 5;
  websocket_config.keep_alive_interval = 5;
  websocket_config.keep_alive_count = 3;
  websocket_config.reconnect_timeout_ms = 0;
  websocket_config.network_timeout_ms = 10000;
  websocket_config.ping_interval_sec = 10;
  ESP_LOGI(kTag,
           "TLS allocation snapshot: internal_free=%lu internal_largest=%lu psram_free=%lu",
           static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  client_ = esp_websocket_client_init(&websocket_config);
  if (client_ == nullptr) {
    stop_locked();
    xSemaphoreGive(client_mutex_);
    return ESP_ERR_NO_MEM;
  }
  result = esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY,
                                         websocket_event, this);
  if (result != ESP_OK) {
    stop_locked();
    xSemaphoreGive(client_mutex_);
    return result;
  }
  result = esp_websocket_client_start(client_);
  if (result != ESP_OK) {
    stop_locked();
    xSemaphoreGive(client_mutex_);
    return result;
  }
  xSemaphoreGive(client_mutex_);
  ESP_LOGI(kTag, "P1 WSS starting; VoiceController owns rediscovery");
  return ESP_OK;
}

void GatewayClient::stop() {
  if (client_mutex_ == nullptr ||
      xSemaphoreTake(client_mutex_, portMAX_DELAY) != pdTRUE) {
    return;
  }
  stop_locked();
  xSemaphoreGive(client_mutex_);
}

void GatewayClient::stop_locked() {
  // Suppress callbacks before tearing down the client. Reconnect scheduling
  // belongs to VoiceController's task, never to the WebSocket event task.
  observer_.store(nullptr);
  if (client_ != nullptr) {
    esp_websocket_client_stop(client_);
    esp_websocket_client_destroy(client_);
    client_ = nullptr;
  }
  receive_size_ = 0;
}

bool GatewayClient::connected() const {
  if (client_mutex_ == nullptr ||
      xSemaphoreTake(client_mutex_, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  const bool result = connected_locked();
  xSemaphoreGive(client_mutex_);
  return result;
}

bool GatewayClient::connected_locked() const {
  return client_ != nullptr && esp_websocket_client_is_connected(client_);
}

esp_err_t GatewayClient::ensure_client_mutex() {
  if (client_mutex_ != nullptr) return ESP_OK;
  client_mutex_ = xSemaphoreCreateMutex();
  return client_mutex_ == nullptr ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t GatewayClient::send_text(const char* data, size_t size,
                                   uint32_t timeout_ms) {
  if (data == nullptr || size == 0 || size > 16384) return ESP_ERR_INVALID_ARG;
  if (client_mutex_ == nullptr ||
      xSemaphoreTake(client_mutex_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  if (!connected_locked()) {
    xSemaphoreGive(client_mutex_);
    return ESP_ERR_INVALID_STATE;
  }
  const int sent = esp_websocket_client_send_text(
      client_, data, static_cast<int>(size), pdMS_TO_TICKS(timeout_ms));
  xSemaphoreGive(client_mutex_);
  return sent == static_cast<int>(size) ? ESP_OK : ESP_FAIL;
}

esp_err_t GatewayClient::send_binary(const uint8_t* data, size_t size,
                                     uint32_t timeout_ms) {
  if (data == nullptr || size == 0 || size > receive_buffer_.size()) {
    return ESP_ERR_INVALID_ARG;
  }
  if (client_mutex_ == nullptr ||
      xSemaphoreTake(client_mutex_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  if (!connected_locked()) {
    xSemaphoreGive(client_mutex_);
    return ESP_ERR_INVALID_STATE;
  }
  const int sent = esp_websocket_client_send_bin(
      client_, reinterpret_cast<const char*>(data), static_cast<int>(size),
      pdMS_TO_TICKS(timeout_ms));
  xSemaphoreGive(client_mutex_);
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
  GatewayObserver* observer = observer_.load();
  if (observer == nullptr) return;
  if (event_id == WEBSOCKET_EVENT_CONNECTED) {
    receive_size_ = 0;
    receive_frame_count_ = 0;
    disconnect_reported_.store(false);
    ESP_LOGI(kTag, "P1 WSS transport connected");
    observer->on_gateway_connected();
    return;
  }
  if (event_id == WEBSOCKET_EVENT_DISCONNECTED ||
      event_id == WEBSOCKET_EVENT_CLOSED) {
    receive_size_ = 0;
    if (disconnect_reported_.exchange(true)) return;
    ESP_LOGW(kTag, "P1 WSS transport disconnected; rediscovery requested");
    observer->on_gateway_disconnected();
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
      observer->on_gateway_text(
          reinterpret_cast<const char*>(receive_buffer_.data()),
          receive_size_);
    }
  } else if (receive_opcode_ == 0x2) {
    observer->on_gateway_binary(receive_buffer_.data(), receive_size_);
  }
  ++receive_frame_count_;
  if (receive_frame_count_ == 1 || receive_frame_count_ % 100 == 0) {
    ESP_LOGI(kTag, "WSS event task stack free=%u bytes after %lu frames",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
             static_cast<unsigned long>(receive_frame_count_));
  }
  receive_size_ = 0;
}

}  // namespace sesame::transport
