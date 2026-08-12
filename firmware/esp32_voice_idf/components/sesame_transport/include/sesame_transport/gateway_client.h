#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_event.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_websocket_client.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "sesame_transport/device_config.h"

namespace sesame::transport {

class GatewayObserver {
 public:
  virtual ~GatewayObserver() = default;
  virtual void on_gateway_connected() = 0;
  virtual void on_gateway_disconnected() = 0;
  virtual void on_gateway_text(const char* data, size_t size) = 0;
  virtual void on_gateway_binary(const uint8_t* data, size_t size) = 0;
};

class GatewayClient {
 public:
  GatewayClient() = default;
  ~GatewayClient();

  GatewayClient(const GatewayClient&) = delete;
  GatewayClient& operator=(const GatewayClient&) = delete;

  esp_err_t start(const StoredDeviceConfig& config, GatewayObserver* observer);
  void stop();
  bool connected() const;
  esp_err_t send_text(const char* data, size_t size,
                      uint32_t timeout_ms = 2000);
  esp_err_t send_binary(const uint8_t* data, size_t size,
                        uint32_t timeout_ms = 2000);

 private:
  static constexpr uint32_t kWebsocketTaskStackBytes = 8192;

  static void wifi_event(void* context, esp_event_base_t event_base,
                         int32_t event_id, void* event_data);
  static void websocket_event(void* handler_arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data);
  void handle_websocket_event(int32_t event_id,
                              const esp_websocket_event_data_t& event);
  void request_wifi_connection();
  void reset_wifi_association_after_timeout();
  void log_wifi_diagnostics_after_timeout(const StoredDeviceConfig& config);
  void publish_web_control_alias(esp_netif_t* station);
  void clear_web_control_alias_address();
  esp_err_t initialize_local_mdns(const StoredDeviceConfig& config,
                                  esp_netif_t* station);
  esp_err_t connect_wifi(const StoredDeviceConfig& config);
  esp_err_t discover_gateway(const StoredDeviceConfig& config);
  esp_err_t ensure_client_mutex();
  void stop_locked();
  bool connected_locked() const;

  std::atomic<GatewayObserver*> observer_{nullptr};
  std::atomic<bool> disconnect_reported_{false};
  esp_websocket_client_handle_t client_{nullptr};
  SemaphoreHandle_t client_mutex_{nullptr};
  std::array<char, 256> uri_{};
  std::array<char, 256> gateway_tls_name_{};
  std::array<char, 300> authorization_{};
  std::array<uint8_t, 2048> receive_buffer_{};
  size_t receive_size_{0};
  uint8_t receive_opcode_{0};
  uint32_t receive_frame_count_{0};
  EventGroupHandle_t wifi_event_group_{nullptr};
  esp_event_handler_instance_t wifi_event_handler_{};
  esp_event_handler_instance_t ip_event_handler_{};
  bool wifi_handlers_registered_{false};
  // Wi-Fi is process-wide on ESP-IDF. These flags make retries preserve the
  // one configured STA instead of trying to start or reconfigure it while it
  // is already associating.
  bool wifi_configured_{false};
  bool wifi_started_{false};
  bool local_mdns_published_{false};
  bool web_control_alias_published_{false};
  std::array<char, 64> web_control_hostname_{};
  std::atomic<bool> wifi_connection_requested_{false};
  bool wifi_timeout_scan_attempted_{false};
  std::atomic<uint8_t> wifi_disconnect_diagnostic_count_{0};
};

}  // namespace sesame::transport
