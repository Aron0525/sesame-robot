#include <cassert>
#include <cstring>

#include "sesame_transport/transport_policy.h"

int main() {
  using sesame::transport::GatewayCandidate;
  using sesame::transport::ReconnectSchedule;
  using sesame::transport::format_device_mdns_hostname;
  using sesame::transport::format_gateway_uri;

  const GatewayCandidate candidate{
      "sesame-gateway",
      8765,
      "gw_001",
      "1",
      "1",
      "/v1/device-stream",
  };
  char uri[128]{};
  assert(format_gateway_uri(candidate, uri, sizeof(uri)));
  assert(std::strcmp(uri, "wss://sesame-gateway.local:8765/v1/device-stream") ==
         0);

  char tls_hostname[128]{};
  assert(format_gateway_tls_hostname(candidate, tls_hostname,
                                     sizeof(tls_hostname)));
  assert(std::strcmp(tls_hostname, "sesame-gateway.local") == 0);

  char ipv4_uri[128]{};
  assert(format_gateway_ipv4_uri(candidate, "192.168.88.21", ipv4_uri,
                                 sizeof(ipv4_uri)));
  assert(std::strcmp(ipv4_uri, "wss://192.168.88.21:8765/v1/device-stream") ==
         0);
  assert(!format_gateway_ipv4_uri(candidate, "not-an-ip", ipv4_uri,
                                  sizeof(ipv4_uri)));

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
