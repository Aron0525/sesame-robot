#include <cassert>
#include <cstring>

#include "sesame_transport/transport_policy.h"

int main() {
  using sesame::transport::GatewayCandidate;
  using sesame::transport::LocalNetworkConfig;
  using sesame::transport::ReconnectSchedule;
  using sesame::transport::format_device_mdns_hostname;
  using sesame::transport::format_gateway_uri;
  using sesame::transport::kGatewayMdnsService;
  using sesame::transport::kLegacyGatewayMdnsService;
  using sesame::transport::parse_gateway_unix_time;
  using sesame::transport::tls_clock_is_plausible;
  using sesame::transport::validate_candidate;
  using sesame::transport::validate_local_network_config;

  const GatewayCandidate candidate{
      "sesame-stream-gateway",
      8766,
      "gw_stream_lab",
      "1",
      "1",
      "/v2/device-stream",
  };
  assert(std::strcmp(kGatewayMdnsService, "_sesame-streamgw") == 0);
  assert(std::strcmp(kLegacyGatewayMdnsService, "_sesame-gw") == 0);
  const LocalNetworkConfig local_network{
      "YuanGuang", "wifi-password", "sesame-v3-001", "sesame-robot",
  };
  assert(validate_local_network_config(local_network));
  assert(!validate_local_network_config({"", "wifi-password", "sesame-v3-001", ""}));
  assert(!validate_local_network_config({"YuanGuang", "", "sesame-v3-001", ""}));
  assert(!validate_local_network_config({"YuanGuang", "wifi-password", "", ""}));
  assert(validate_candidate(candidate, "gw_stream_lab") ==
         sesame::transport::CandidateError::kOk);
  char uri[128]{};
  assert(format_gateway_uri(candidate, uri, sizeof(uri)));
  assert(std::strcmp(uri, "wss://sesame-stream-gateway.local:8766/v2/device-stream") ==
         0);

  char tls_hostname[128]{};
  assert(format_gateway_tls_hostname(candidate, tls_hostname,
                                     sizeof(tls_hostname)));
  assert(std::strcmp(tls_hostname, "sesame-stream-gateway.local") == 0);

  char ipv4_uri[128]{};
  assert(format_gateway_ipv4_uri(candidate, "192.168.88.21", ipv4_uri,
                                 sizeof(ipv4_uri)));
  assert(std::strcmp(ipv4_uri, "wss://192.168.88.21:8766/v2/device-stream") ==
         0);
  assert(!format_gateway_ipv4_uri(candidate, "not-an-ip", ipv4_uri,
                                  sizeof(ipv4_uri)));

  const GatewayCandidate legacy_candidate{
      "sesame-gateway", 8765, "gw_stream_lab", "1", "1",
      "/v1/device-stream",
  };
  assert(validate_candidate(legacy_candidate, "gw_stream_lab") ==
         sesame::transport::CandidateError::kOk);

  int64_t gateway_unix_time = 0;
  assert(parse_gateway_unix_time("1787400000", &gateway_unix_time));
  assert(gateway_unix_time == 1'787'400'000);
  assert(!parse_gateway_unix_time("", &gateway_unix_time));
  assert(!parse_gateway_unix_time("1787400000x", &gateway_unix_time));
  assert(!parse_gateway_unix_time("99999999999999999999", &gateway_unix_time));

  assert(!tls_clock_is_plausible(0));
  assert(!tls_clock_is_plausible(1'577'836'799));
  assert(tls_clock_is_plausible(1'577'836'800));

  char device_hostname[64]{};
  assert(format_device_mdns_hostname("sesame_v3_001", device_hostname,
                                     sizeof(device_hostname)));
  assert(std::strcmp(device_hostname, "sesame-v3-001") == 0);

  ReconnectSchedule schedule;
  assert(schedule.next_delay_ms() == 500);
  assert(schedule.next_delay_ms() == 1000);
  assert(schedule.next_delay_ms() == 2000);
  schedule.reset();
  assert(schedule.next_delay_ms() == 500);
}
