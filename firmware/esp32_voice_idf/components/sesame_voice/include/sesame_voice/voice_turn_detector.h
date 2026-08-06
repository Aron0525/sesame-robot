#pragma once

#include <cstdint>

namespace sesame::voice {

enum class VoiceTurnEvent {
  kNone,
  kWakeDetected,
  kListenStarted,
  kListenStopped,
  kWakeTimedOut,
  kListenTimedOut,
};

struct VoiceTurnDetectorConfig {
  uint32_t wake_to_speech_timeout_ms;
  uint32_t minimum_listen_ms;
  uint32_t endpoint_silence_ms;
  uint32_t maximum_listen_ms;
};

inline constexpr VoiceTurnDetectorConfig kWakeVoiceTurnConfig{
    .wake_to_speech_timeout_ms = 3000,
    .minimum_listen_ms = 0,
    .endpoint_silence_ms = 2000,
    .maximum_listen_ms = 30000,
};

class VoiceTurnDetector {
 public:
  explicit VoiceTurnDetector(VoiceTurnDetectorConfig config) : config_(config) {}

  VoiceTurnEvent start_listening(uint64_t now_ms);
  void begin_waiting_for_speech(uint64_t now_ms);
  VoiceTurnEvent update(uint64_t now_ms, bool wake_detected, bool vad_speech);
  void reset();
  bool listening() const;

 private:
  enum class State { kWaitingForWake, kWaitingForSpeech, kListening };

  VoiceTurnDetectorConfig config_;
  State state_{State::kWaitingForWake};
  uint64_t state_started_ms_{0};
  uint64_t listen_started_ms_{0};
  uint64_t last_speech_ms_{0};
};

}  // namespace sesame::voice
