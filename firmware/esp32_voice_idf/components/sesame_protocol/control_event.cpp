#include "sesame_protocol/control_event.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace sesame::protocol {
namespace {

struct TypeName {
  ControlEventType type;
  const char* name;
};

constexpr TypeName kTypeNames[] = {
    {ControlEventType::kSessionHello, "session.hello"},
    {ControlEventType::kSessionReady, "session.ready"},
    {ControlEventType::kListenStart, "listen.start"},
    {ControlEventType::kListenStop, "listen.stop"},
    {ControlEventType::kInterrupt, "interrupt"},
    {ControlEventType::kAsrPartial, "asr.partial"},
    {ControlEventType::kAsrFinal, "asr.final"},
    {ControlEventType::kAgentReply, "agent.reply"},
    {ControlEventType::kResponsePlan, "response.plan"},
    {ControlEventType::kTurnComplete, "turn.complete"},
    {ControlEventType::kTtsStart, "tts.start"},
    {ControlEventType::kTtsStop, "tts.stop"},
    {ControlEventType::kTtsFlush, "tts.flush"},
    {ControlEventType::kExpressionSet, "expression.set"},
    {ControlEventType::kActionExecute, "action.execute"},
    {ControlEventType::kOperatorControl, "operator.control"},
    {ControlEventType::kActionResult, "action.result"},
    {ControlEventType::kError, "error"},
};

bool is_identifier_character(char value) {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '_' || value == '-' ||
         value == '.' || value == ':';
}

bool is_valid_identifier(const char* value) {
  if (value == nullptr) {
    return true;
  }
  const size_t length = strnlen(value, kMaxControlIdentifierBytes + 1);
  if (length == 0 || length > kMaxControlIdentifierBytes) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    if (!is_identifier_character(value[index])) {
      return false;
    }
  }
  return true;
}

bool is_valid_payload(const char* payload) {
  if (payload == nullptr) {
    return false;
  }
  const size_t length = strnlen(payload, kMaxControlPayloadBytes + 1);
  return length >= 2 && length <= kMaxControlPayloadBytes &&
         payload[0] == '{' && payload[length - 1] == '}';
}

int append_nullable_identifier(char* output, size_t output_capacity,
                               const char* value) {
  if (value == nullptr) {
    return std::snprintf(output, output_capacity, "null");
  }
  return std::snprintf(output, output_capacity, "\"%s\"", value);
}

}  // namespace

const char* to_string(ControlEventType type) {
  for (const TypeName& entry : kTypeNames) {
    if (entry.type == type) {
      return entry.name;
    }
  }
  return "unknown";
}

ControlEventType control_event_type_from_string(const char* name) {
  if (name == nullptr) {
    return ControlEventType::kUnknown;
  }
  for (const TypeName& entry : kTypeNames) {
    if (std::strcmp(entry.name, name) == 0) {
      return entry.type;
    }
  }
  return ControlEventType::kUnknown;
}

ControlEventError serialize_control_event(const ControlEvent& event,
                                          char* output,
                                          size_t output_capacity,
                                          size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }
  if (output == nullptr || bytes_written == nullptr || output_capacity == 0) {
    return ControlEventError::kNullArgument;
  }
  if (event.type == ControlEventType::kUnknown) {
    return ControlEventError::kUnknownType;
  }
  if (!is_valid_identifier(event.session_id) ||
      !is_valid_identifier(event.turn_id) ||
      !is_valid_identifier(event.request_id)) {
    return ControlEventError::kInvalidIdentifier;
  }
  if (!is_valid_payload(event.payload_json)) {
    return ControlEventError::kInvalidPayload;
  }

  size_t offset = 0;
  const int prefix_length = std::snprintf(
      output, output_capacity, "{\"v\":1,\"type\":\"%s\",\"session_id\":",
      to_string(event.type));
  if (prefix_length < 0 ||
      static_cast<size_t>(prefix_length) >= output_capacity) {
    return ControlEventError::kOutputTooSmall;
  }
  offset = static_cast<size_t>(prefix_length);

  const char* nullable_values[] = {event.session_id, event.turn_id,
                                   event.request_id};
  const char* following_keys[] = {",\"turn_id\":", ",\"request_id\":", ""};
  for (size_t index = 0; index < 3; ++index) {
    const int value_length = append_nullable_identifier(
        output + offset, output_capacity - offset, nullable_values[index]);
    if (value_length < 0 ||
        static_cast<size_t>(value_length) >= output_capacity - offset) {
      return ControlEventError::kOutputTooSmall;
    }
    offset += static_cast<size_t>(value_length);

    const int key_length =
        std::snprintf(output + offset, output_capacity - offset, "%s",
                      following_keys[index]);
    if (key_length < 0 ||
        static_cast<size_t>(key_length) >= output_capacity - offset) {
      return ControlEventError::kOutputTooSmall;
    }
    offset += static_cast<size_t>(key_length);
  }

  const int suffix_length = std::snprintf(
      output + offset, output_capacity - offset,
      ",\"sequence\":%" PRIu32 ",\"timestamp_ms\":%" PRIu64
      ",\"payload\":%s}",
      event.sequence, event.timestamp_ms, event.payload_json);
  if (suffix_length < 0 ||
      static_cast<size_t>(suffix_length) >= output_capacity - offset) {
    return ControlEventError::kOutputTooSmall;
  }
  offset += static_cast<size_t>(suffix_length);

  if (offset > kMaxControlFrameBytes) {
    output[0] = '\0';
    return ControlEventError::kOutputTooSmall;
  }
  *bytes_written = offset;
  return ControlEventError::kOk;
}

}  // namespace sesame::protocol
