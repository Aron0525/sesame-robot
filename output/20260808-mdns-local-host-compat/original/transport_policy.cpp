#include "sesame_transport/transport_policy.h"

#include <algorithm>
#include <cstdio>

namespace sesame::transport {

uint32_t reconnect_delay_ms(uint32_t attempt) {
  constexpr uint32_t kInitialMs = 500;
  constexpr uint32_t kMaximumMs = 30000;
  if (attempt >= 6) {
    return kMaximumMs;
  }
  return std::min(kInitialMs << attempt, kMaximumMs);
}

std::optional<uint32_t> ReconnectScheduler::schedule() {
  bool expected = false;
  if (!scheduled_.compare_exchange_strong(expected, true)) {
    return std::nullopt;
  }
  return reconnect_delay_ms(attempt_.fetch_add(1));
}

void ReconnectScheduler::on_timer_fired() { scheduled_.store(false); }

void ReconnectScheduler::on_got_ip() {
  attempt_.store(0);
  scheduled_.store(false);
}

CandidateError validate_candidate(const GatewayCandidate& candidate,
                                  std::string_view expected_gateway_id) {
  if (candidate.host.empty()) return CandidateError::kMissingHost;
  if (candidate.port == 0) return CandidateError::kInvalidPort;
  if (candidate.gateway_id != expected_gateway_id) {
    return CandidateError::kGatewayMismatch;
  }
  if (candidate.protocol != "1") return CandidateError::kProtocolMismatch;
  if (candidate.tls != "1") return CandidateError::kTlsRequired;
  if (candidate.path != "/v1/device-stream") {
    return CandidateError::kPathMismatch;
  }
  return CandidateError::kOk;
}

ConfigError validate_device_config(const DeviceConfig& config) {
  if (config.wifi_ssid.empty() || config.wifi_password.empty()) {
    return ConfigError::kMissingWifi;
  }
  if (config.device_id.empty()) return ConfigError::kMissingDeviceId;
  if (config.gateway_id.empty()) return ConfigError::kMissingGatewayId;
  if (!config.gateway_url.empty()) {
    if (!config.gateway_url.starts_with("wss://")) {
      return ConfigError::kGatewayUrlMustUseWss;
    }
    constexpr std::string_view kDeviceStreamPath = "/v1/device-stream";
    if (!config.gateway_url.ends_with(kDeviceStreamPath)) {
      return ConfigError::kGatewayUrlPathMismatch;
    }
  }
  if (config.device_token.empty()) return ConfigError::kMissingToken;
  if (config.root_ca.empty()) return ConfigError::kMissingRootCa;
  return ConfigError::kOk;
}

void redact_bearer(std::string_view value, char* output, size_t capacity) {
  if (output == nullptr || capacity == 0) return;
  if (value.size() < 16) {
    std::snprintf(output, capacity, "[redacted]");
    return;
  }
  const std::string_view prefix = value.substr(0, 11);
  const std::string_view suffix = value.substr(value.size() - 4);
  std::snprintf(output, capacity, "%.*s...%.*s",
                static_cast<int>(prefix.size()), prefix.data(),
                static_cast<int>(suffix.size()), suffix.data());
}

}  // namespace sesame::transport
