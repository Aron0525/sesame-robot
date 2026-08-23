#pragma once

#include <atomic>
#include <cstddef>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sesame::transport {

struct GatewayCandidate {
  std::string_view host;
  uint16_t port;
  std::string_view gateway_id;
  std::string_view protocol;
  std::string_view tls;
  std::string_view path;
};

struct DeviceConfig {
  std::string_view wifi_ssid;
  std::string_view wifi_password;
  std::string_view device_id;
  std::string_view gateway_id;
  std::string_view gateway_url;
  std::string_view device_token;
  std::string_view root_ca;
};

enum class CandidateError {
  kOk = 0,
  kMissingHost,
  kInvalidPort,
  kGatewayMismatch,
  kProtocolMismatch,
  kTlsRequired,
  kPathMismatch,
};

enum class ConfigError {
  kOk = 0,
  kMissingWifi,
  kMissingDeviceId,
  kMissingGatewayId,
  kGatewayUrlMustUseWss,
  kGatewayUrlPathMismatch,
  kMissingToken,
  kMissingRootCa,
};

uint32_t reconnect_delay_ms(uint32_t attempt);

// Thread-safe state for ensuring that only one Wi-Fi reconnect timer is
// pending. A successful DHCP lease resets the backoff for the next outage.
class ReconnectScheduler {
 public:
  std::optional<uint32_t> schedule();
  void on_timer_fired();
  void on_got_ip();

 private:
  std::atomic_uint32_t attempt_{0};
  std::atomic_bool scheduled_{false};
};

CandidateError validate_candidate(const GatewayCandidate& candidate,
                                  std::string_view expected_gateway_id);
bool format_discovered_gateway_uri(std::string_view hostname, uint16_t port,
                                   char* output, size_t capacity);
ConfigError validate_device_config(const DeviceConfig& config);
void redact_bearer(std::string_view value, char* output, size_t capacity);

}  // namespace sesame::transport
