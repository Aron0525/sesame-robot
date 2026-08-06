#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::protocol {

inline constexpr uint8_t kControlProtocolVersion = 1;
inline constexpr size_t kMaxControlFrameBytes = 16384;
inline constexpr size_t kMaxControlIdentifierBytes = 100;
inline constexpr size_t kMaxControlPayloadBytes = 8192;

enum class ControlEventType {
  kUnknown = 0,
  kSessionHello,
  kSessionReady,
  kListenStart,
  kListenStop,
  kInterrupt,
  kAsrPartial,
  kAsrFinal,
  kAgentReply,
  kResponsePlan,
  kTtsStart,
  kTtsStop,
  kTtsFlush,
  kExpressionSet,
  kActionExecute,
  kOperatorControl,
  kActionResult,
  kError,
};

enum class ControlEventError {
  kOk = 0,
  kNullArgument,
  kUnknownType,
  kInvalidIdentifier,
  kInvalidPayload,
  kOutputTooSmall,
};

struct ControlEvent {
  ControlEventType type{ControlEventType::kUnknown};
  const char* session_id{nullptr};
  const char* turn_id{nullptr};
  const char* request_id{nullptr};
  uint32_t sequence{0};
  uint64_t timestamp_ms{0};
  const char* payload_json{"{}"};
};

const char* to_string(ControlEventType type);
ControlEventType control_event_type_from_string(const char* name);

ControlEventError serialize_control_event(const ControlEvent& event,
                                          char* output,
                                          size_t output_capacity,
                                          size_t* bytes_written);

}  // namespace sesame::protocol
