#pragma once

#include <cstdint>

namespace sesame::voice {

// Events drive network/audio side effects. The detector itself owns no I2S,
// gateway, codec, or AEC state, which keeps its timing behavior testable.
enum class VoiceTurnEvent {
  kNone,
  kWakeDetected,
  kBargeInDetected,
  kListenStarted,
  kListenStopped,
  kWakeTimedOut,
  kFollowupTimedOut,
  kListenTimedOut,
};

enum class VoiceTurnState {
  kIdleWakeListening,
  kWakeAcknowledging,
  kWaitingForFirstSpeech,
  kTtsPlaying,
  kCollectingUserSpeech,
  kWaitingForFollowupSpeech,
};

struct VoiceTurnDetectorConfig {
  uint32_t first_speech_timeout_ms;
  uint32_t endpoint_silence_ms;
  uint32_t maximum_listen_ms;
  uint8_t speech_start_frames;
  uint8_t speech_continue_frames;
};

class VoiceTurnDetector {
 public:
  explicit VoiceTurnDetector(VoiceTurnDetectorConfig config) : config_(config) {}

  // BOOT starts a manual turn immediately. Wake-word turns first enter
  // kWakeAcknowledging and wait for the local "我在" prompt to finish.
  VoiceTurnEvent start_from_button(uint64_t now_ms);
  VoiceTurnEvent update(uint64_t now_ms, bool wake_detected, bool vad_speech);
  bool start_first_speech_wait(uint64_t now_ms);
  bool start_tts_playback();
  bool start_followup_wait(uint64_t now_ms);
  bool waiting_for_speech() const;
  void reset();

  bool listening() const;
  VoiceTurnState state() const { return state_; }

 private:
  void start_collecting(uint64_t now_ms);
  bool speech_confirmed(bool vad_speech, uint8_t required_frames);

  VoiceTurnDetectorConfig config_;
  VoiceTurnState state_{VoiceTurnState::kIdleWakeListening};
  uint64_t wait_started_ms_{0};
  uint64_t listen_started_ms_{0};
  uint64_t last_speech_ms_{0};
  uint8_t consecutive_speech_frames_{0};
};

}  // namespace sesame::voice
