#include <array>
#include <cassert>
#include <cstring>

#include "sesame_protocol/control_event.h"

using sesame::protocol::ControlEvent;
using sesame::protocol::ControlEventError;
using sesame::protocol::ControlEventType;

int main() {
  assert(sesame::protocol::control_event_type_from_string("listen.start") ==
         ControlEventType::kListenStart);
  assert(sesame::protocol::control_event_type_from_string("operator.control") ==
         ControlEventType::kOperatorControl);
  assert(sesame::protocol::control_event_type_from_string("playback.stats") ==
         ControlEventType::kPlaybackStats);
  assert(sesame::protocol::control_event_type_from_string("not.allowed") ==
         ControlEventType::kUnknown);

  const ControlEvent event{
      .type = ControlEventType::kListenStart,
      .session_id = "ses_001",
      .turn_id = "turn_001",
      .request_id = nullptr,
      .sequence = 9,
      .timestamp_ms = 1234,
      .payload_json = "{\"trigger\":\"manual\"}",
  };
  std::array<char, 512> output{};
  size_t written = 0;
  assert(sesame::protocol::serialize_control_event(
             event, output.data(), output.size(), &written) ==
         ControlEventError::kOk);
  assert(written == std::strlen(output.data()));
  assert(std::strstr(output.data(), "\"v\":1") != nullptr);
  assert(std::strstr(output.data(), "\"type\":\"listen.start\"") != nullptr);
  assert(std::strstr(output.data(), "\"trigger\":\"manual\"") != nullptr);
  assert(std::strstr(output.data(), "\"request_id\":null") != nullptr);

  const ControlEvent playback_stats{
      .type = ControlEventType::kPlaybackStats,
      .session_id = "ses_001",
      .turn_id = "turn_001",
      .request_id = nullptr,
      .sequence = 10,
      .timestamp_ms = 123457,
      .payload_json =
          "{\"generation_id\":7,\"buffered_packets\":30,\"max_buffered_packets\":30,\"low_watermark_packets\":20,\"high_watermark_packets\":40,\"underflow_count\":0,\"playback_started\":true,\"paused\":false}",
  };
  std::array<char, 512> playback_output{};
  size_t playback_written = 0;
  assert(sesame::protocol::serialize_control_event(
             playback_stats, playback_output.data(), playback_output.size(),
             &playback_written) == ControlEventError::kOk);
  assert(playback_written > 0);
  assert(std::strstr(playback_output.data(),
                     "\"type\":\"playback.stats\"") != nullptr);

  ControlEvent invalid = event;
  invalid.turn_id = "turn_\"injection";
  assert(sesame::protocol::serialize_control_event(
             invalid, output.data(), output.size(), &written) ==
         ControlEventError::kInvalidIdentifier);
  return 0;
}
