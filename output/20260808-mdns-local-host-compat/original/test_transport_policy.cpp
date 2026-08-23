#include <cassert>
#include <cstring>

#include "sesame_transport/transport_policy.h"

int main() {
  using namespace sesame::transport;

  assert(reconnect_delay_ms(0) == 500);
  assert(reconnect_delay_ms(1) == 1000);
  assert(reconnect_delay_ms(9) == 30000);

  ReconnectScheduler reconnect_scheduler;
  assert(reconnect_scheduler.schedule().value() == 500);
  assert(!reconnect_scheduler.schedule().has_value());
  reconnect_scheduler.on_timer_fired();
  assert(reconnect_scheduler.schedule().value() == 1000);
  reconnect_scheduler.on_got_ip();
  assert(reconnect_scheduler.schedule().value() == 500);

  GatewayCandidate good{"sesame-computer.local", 8765, "gateway-main", "1",
                        "1", "/v1/device-stream"};
  assert(validate_candidate(good, "gateway-main") == CandidateError::kOk);
  good.tls = "0";
  assert(validate_candidate(good, "gateway-main") ==
         CandidateError::kTlsRequired);

  DeviceConfig valid{"ssid", "password", "device-001", "gateway-main",
                     "wss://voice.example.com/v1/device-stream",
                     "token-value", "-----BEGIN CERTIFICATE-----"};
  assert(validate_device_config(valid) == ConfigError::kOk);
  valid.device_token = "";
  assert(validate_device_config(valid) == ConfigError::kMissingToken);

  valid.device_token = "token-value";
  valid.gateway_url = "";
  assert(validate_device_config(valid) == ConfigError::kOk);

  valid.gateway_url = "ws://voice.example.com/v1/device-stream";
  assert(validate_device_config(valid) == ConfigError::kGatewayUrlMustUseWss);

  valid.gateway_url = "wss://voice.example.com/not-device-stream";
  assert(validate_device_config(valid) == ConfigError::kGatewayUrlPathMismatch);

  char output[32]{};
  redact_bearer("Bearer abcdefghijklmnop", output, sizeof(output));
  assert(std::strcmp(output, "Bearer abcd...mnop") == 0);
}
