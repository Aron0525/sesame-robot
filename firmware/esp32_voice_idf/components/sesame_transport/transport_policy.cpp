#include "sesame_transport/transport_policy.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace sesame::transport {
namespace {

bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

bool is_safe_mdns_hostname(std::string_view value) {
  if (value.empty()) return false;
  for (const char character : value) {
    const bool is_lowercase_letter = character >= 'a' && character <= 'z';
    const bool is_uppercase_letter = character >= 'A' && character <= 'Z';
    const bool is_digit = character >= '0' && character <= '9';
    if (!is_lowercase_letter && !is_uppercase_letter && !is_digit &&
        character != '-' && character != '.') {
      return false;
    }
  }
  return true;
}

bool is_ipv4_literal(std::string_view value) {
  uint32_t octet = 0;
  uint8_t octet_count = 0;
  uint8_t digit_count = 0;
  for (const char character : value) {
    if (character >= '0' && character <= '9') {
      if (++digit_count > 3) return false;
      octet = octet * 10 + static_cast<uint32_t>(character - '0');
      if (octet > 255) return false;
      continue;
    }
    if (character != '.' || digit_count == 0 || octet_count >= 3) {
      return false;
    }
    ++octet_count;
    octet = 0;
    digit_count = 0;
  }
  return octet_count == 3 && digit_count > 0;
}

bool format_uri_with_host(const GatewayCandidate& candidate,
                          std::string_view host, char* output,
                          size_t capacity) {
  if (output == nullptr || capacity == 0 || host.empty() ||
      candidate.port == 0 || candidate.path.empty() ||
      candidate.path.front() != '/') {
    return false;
  }
  const int written = std::snprintf(
      output, capacity, "wss://%.*s:%u%.*s", static_cast<int>(host.size()),
      host.data(), candidate.port, static_cast<int>(candidate.path.size()),
      candidate.path.data());
  return written > 0 && static_cast<size_t>(written) < capacity;
}

}  // namespace

uint32_t reconnect_delay_ms(uint32_t attempt) {
  constexpr uint32_t kInitialMs = 500;
  constexpr uint32_t kMaximumMs = 30000;
  if (attempt >= 6) {
    return kMaximumMs;
  }
  return std::min(kInitialMs << attempt, kMaximumMs);
}

uint32_t ReconnectSchedule::next_delay_ms() {
  const uint32_t delay = reconnect_delay_ms(attempt_);
  if (attempt_ < 6) ++attempt_;
  return delay;
}

void ReconnectSchedule::reset() { attempt_ = 0; }

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

bool format_gateway_tls_hostname(const GatewayCandidate& candidate,
                                 char* output, size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  std::string_view host = candidate.host;
  if (ends_with(host, ".local.")) host.remove_suffix(1);
  if (!is_safe_mdns_hostname(host)) return false;

  const bool needs_local_suffix = !ends_with(host, ".local");
  if (needs_local_suffix && host.find('.') != std::string_view::npos) {
    return false;
  }
  const int written = std::snprintf(output, capacity, "%.*s%s",
                                    static_cast<int>(host.size()), host.data(),
                                    needs_local_suffix ? ".local" : "");
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool format_gateway_uri(const GatewayCandidate& candidate, char* output,
                        size_t capacity) {
  std::array<char, 256> hostname{};
  if (!format_gateway_tls_hostname(candidate, hostname.data(),
                                   hostname.size())) {
    return false;
  }
  return format_uri_with_host(candidate, hostname.data(), output, capacity);
}

bool format_gateway_ipv4_uri(const GatewayCandidate& candidate,
                             std::string_view ipv4_address, char* output,
                             size_t capacity) {
  if (!is_ipv4_literal(ipv4_address)) return false;
  return format_uri_with_host(candidate, ipv4_address, output, capacity);
}

bool format_device_mdns_hostname(std::string_view device_id, char* output,
                                 size_t capacity) {
  constexpr size_t kMdnsLabelMaximumLength = 63;
  if (output == nullptr || capacity < 2 || device_id.empty() ||
      device_id.size() > kMdnsLabelMaximumLength ||
      device_id.size() >= capacity) {
    return false;
  }

  for (size_t index = 0; index < device_id.size(); ++index) {
    const char character = device_id[index];
    const bool is_lowercase_letter = character >= 'a' && character <= 'z';
    const bool is_uppercase_letter = character >= 'A' && character <= 'Z';
    const bool is_digit = character >= '0' && character <= '9';
    if (!is_lowercase_letter && !is_uppercase_letter && !is_digit &&
        character != '-' && character != '_') {
      return false;
    }
    output[index] = character == '_' ? '-' : character;
  }
  if (output[0] == '-' || output[device_id.size() - 1] == '-') return false;
  output[device_id.size()] = '\0';
  return true;
}

ConfigError validate_device_config(const DeviceConfig& config) {
  if (config.wifi_ssid.empty() || config.wifi_password.empty()) {
    return ConfigError::kMissingWifi;
  }
  if (config.device_id.empty()) return ConfigError::kMissingDeviceId;
  if (config.gateway_id.empty()) return ConfigError::kMissingGatewayId;
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
