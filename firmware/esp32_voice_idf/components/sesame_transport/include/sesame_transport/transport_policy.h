#pragma once

#include <cstddef>
#include <cstdint>
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
  kMissingToken,
  kMissingRootCa,
};

uint32_t reconnect_delay_ms(uint32_t attempt);

class ReconnectSchedule {
 public:
  uint32_t next_delay_ms();
  void reset();

 private:
  uint32_t attempt_{0};
};

CandidateError validate_candidate(const GatewayCandidate& candidate,
                                  std::string_view expected_gateway_id);
bool format_gateway_tls_hostname(const GatewayCandidate& candidate,
                                 char* output, size_t capacity);
bool format_gateway_uri(const GatewayCandidate& candidate, char* output,
                        size_t capacity);
bool format_gateway_ipv4_uri(const GatewayCandidate& candidate,
                             std::string_view ipv4_address, char* output,
                             size_t capacity);
bool format_device_mdns_hostname(std::string_view device_id, char* output,
                                 size_t capacity);
ConfigError validate_device_config(const DeviceConfig& config);
void redact_bearer(std::string_view value, char* output, size_t capacity);

}  // namespace sesame::transport
