#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sesame::transport {

inline constexpr char kGatewayMdnsService[] = "_sesame-streamgw";
inline constexpr char kLegacyGatewayMdnsService[] = "_sesame-gw";

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

// The local control network deliberately has a smaller configuration
// boundary than the Gateway: it must keep working even when credentials for
// mDNS/WSS/TLS are absent or invalid.
struct LocalNetworkConfig {
  std::string_view wifi_ssid;
  std::string_view wifi_password;
  std::string_view device_id;
  std::string_view web_control_hostname;
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
bool tls_clock_is_plausible(int64_t unix_seconds);
bool parse_gateway_unix_time(std::string_view value, int64_t* unix_seconds);
ConfigError validate_device_config(const DeviceConfig& config);
bool validate_local_network_config(const LocalNetworkConfig& config);
void redact_bearer(std::string_view value, char* output, size_t capacity);

}  // namespace sesame::transport
