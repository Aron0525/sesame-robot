#include "sesame_voice/voice_turn_detector.h"

namespace sesame::voice {

VoiceTurnEvent VoiceTurnDetector::start_listening(uint64_t now_ms) {
  state_ = State::kListening;
  listen_started_ms_ = now_ms;
  last_speech_ms_ = now_ms;
  return VoiceTurnEvent::kListenStarted;
}

void VoiceTurnDetector::begin_waiting_for_speech(uint64_t now_ms) {
  state_ = State::kWaitingForSpeech;
  state_started_ms_ = now_ms;
  listen_started_ms_ = 0;
  last_speech_ms_ = 0;
}

VoiceTurnEvent VoiceTurnDetector::update(uint64_t now_ms, bool wake_detected,
                                          bool vad_speech) {
  switch (state_) {
    case State::kWaitingForWake:
      if (!wake_detected) return VoiceTurnEvent::kNone;
      begin_waiting_for_speech(now_ms);
      return VoiceTurnEvent::kWakeDetected;

    case State::kWaitingForSpeech:
      if (vad_speech) {
        state_ = State::kListening;
        listen_started_ms_ = now_ms;
        last_speech_ms_ = now_ms;
        return VoiceTurnEvent::kListenStarted;
      }
      if (now_ms - state_started_ms_ >= config_.wake_to_speech_timeout_ms) {
        reset();
        return VoiceTurnEvent::kWakeTimedOut;
      }
      return VoiceTurnEvent::kNone;

    case State::kListening:
      if (vad_speech) last_speech_ms_ = now_ms;
      if (now_ms - listen_started_ms_ >= config_.maximum_listen_ms) {
        reset();
        return VoiceTurnEvent::kListenTimedOut;
      }
      if (now_ms - listen_started_ms_ >= config_.minimum_listen_ms &&
          now_ms - last_speech_ms_ >= config_.endpoint_silence_ms) {
        reset();
        return VoiceTurnEvent::kListenStopped;
      }
      return VoiceTurnEvent::kNone;
  }
  return VoiceTurnEvent::kNone;
}

void VoiceTurnDetector::reset() {
  state_ = State::kWaitingForWake;
  state_started_ms_ = 0;
  listen_started_ms_ = 0;
  last_speech_ms_ = 0;
}

bool VoiceTurnDetector::listening() const { return state_ == State::kListening; }

}  // namespace sesame::voice
