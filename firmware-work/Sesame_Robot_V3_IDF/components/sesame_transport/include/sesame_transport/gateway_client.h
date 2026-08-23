#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_event.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/event_groups.h"

#include "sesame_transport/device_config.h"
#include "sesame_transport/transport_policy.h"

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
  esp_err_t force_reconnect();
  bool connected() const;
  esp_err_t send_text(const char* data, size_t size);
  esp_err_t send_binary(const uint8_t* data, size_t size);

 private:
  static void wifi_event(void* handler_arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data);
  static void reconnect_timer_callback(void* callback_arg);
  static void websocket_event(void* handler_arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data);
  void handle_wifi_event(esp_event_base_t event_base, int32_t event_id,
                         const void* event_data);
  void schedule_wifi_reconnect();
  void reconnect_wifi();
  void handle_websocket_event(int32_t event_id,
                              const esp_websocket_event_data_t& event);
  esp_err_t setup_wifi_events();
  void cleanup_wifi_events();
  esp_err_t connect_wifi(const StoredDeviceConfig& config);
  esp_err_t discover_gateway(const StoredDeviceConfig& config);

  GatewayObserver* observer_{nullptr};
  esp_websocket_client_handle_t client_{nullptr};
  EventGroupHandle_t wifi_events_{nullptr};
  esp_event_handler_instance_t wifi_event_instance_{nullptr};
  esp_event_handler_instance_t got_ip_event_instance_{nullptr};
  esp_timer_handle_t reconnect_timer_{nullptr};
  ReconnectScheduler reconnect_scheduler_{};
  std::atomic_bool stopping_{true};
  std::atomic_bool wifi_connected_{false};
  std::array<char, 256> uri_{};
  std::array<char, 256> certificate_common_name_{};
  std::array<char, 300> authorization_{};
  std::array<uint8_t, 2048> receive_buffer_{};
  size_t receive_size_{0};
  uint8_t receive_opcode_{0};
};

}  // namespace sesame::transport
