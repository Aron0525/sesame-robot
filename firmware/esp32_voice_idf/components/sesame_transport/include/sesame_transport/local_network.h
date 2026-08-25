#pragma once

#include <atomic>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "sesame_transport/transport_policy.h"

namespace sesame::transport {

// Owns infrastructure Wi-Fi (STA) and local mDNS independently from the
// voice Gateway, TLS, or NVS.
class LocalNetwork {
 public:
  LocalNetwork() = default;
  ~LocalNetwork();

  LocalNetwork(const LocalNetwork&) = delete;
  LocalNetwork& operator=(const LocalNetwork&) = delete;

  esp_err_t start(const LocalNetworkConfig& config);

 private:
  static void wifi_event(void* context, esp_event_base_t event_base,
                         int32_t event_id, void* event_data);
  void request_station_connection();
  esp_err_t start_mdns(const LocalNetworkConfig& config);

  esp_netif_t* station_{nullptr};
  esp_event_handler_instance_t wifi_event_handler_{};
  esp_event_handler_instance_t ip_event_handler_{};
  bool event_handlers_registered_{false};
  bool started_{false};
  bool station_enabled_{false};
  std::atomic<bool> station_connection_requested_{false};
};

}  // namespace sesame::transport
