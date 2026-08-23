#pragma once

#include <cstdint>

namespace sesame::voice {

enum class VoiceTurnEvent {
  kNone,
  kWakeDetected,
  kListenStarted,
  kFollowUpListenStarted,
  kListenStopped,
  kWakeTimedOut,
  kFollowUpTimedOut,
  kListenTimedOut,
};

enum class CaptureTrigger {
  kManual,
  kWakeword,
  kFollowup,
};

constexpr const char* capture_trigger_name(CaptureTrigger trigger) {
  switch (trigger) {
    case CaptureTrigger::kManual:
      return "manual";
    case CaptureTrigger::kWakeword:
      return "wakeword";
    case CaptureTrigger::kFollowup:
      return "followup";
  }
  return "manual";
}

struct VoiceTurnDetectorConfig {
  uint32_t wake_to_speech_timeout_ms;
  uint32_t endpoint_silence_ms;
  uint32_t maximum_listen_ms;
};

class VoiceTurnDetector {
 public:
  explicit VoiceTurnDetector(VoiceTurnDetectorConfig config) : config_(config) {}

  VoiceTurnEvent start_from_button(uint64_t now_ms);
  VoiceTurnEvent start_follow_up(uint64_t now_ms);
  VoiceTurnEvent update(uint64_t now_ms, bool wake_detected, bool vad_speech);
  void reset();
  bool listening() const;

 private:
  enum class State { kWaitingForWake, kListening };

  VoiceTurnDetectorConfig config_;
  State state_{State::kWaitingForWake};
  uint64_t listen_started_ms_{0};
  uint64_t last_speech_ms_{0};
  bool waiting_for_first_speech_{false};
  bool follow_up_window_{false};
  bool manual_capture_{false};
};

}  // namespace sesame::voice
