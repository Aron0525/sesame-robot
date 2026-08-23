#include "sesame_voice/voice_turn_detector.h"

namespace sesame::voice {

VoiceTurnEvent VoiceTurnDetector::start_from_button(uint64_t now_ms) {
  state_ = State::kListening;
  listen_started_ms_ = now_ms;
  last_speech_ms_ = now_ms;
  waiting_for_first_speech_ = false;
  follow_up_window_ = false;
  manual_capture_ = true;
  return VoiceTurnEvent::kListenStarted;
}

VoiceTurnEvent VoiceTurnDetector::start_follow_up(uint64_t now_ms) {
  state_ = State::kListening;
  listen_started_ms_ = now_ms;
  last_speech_ms_ = now_ms;
  waiting_for_first_speech_ = true;
  follow_up_window_ = true;
  manual_capture_ = false;
  return VoiceTurnEvent::kFollowUpListenStarted;
}

VoiceTurnEvent VoiceTurnDetector::update(uint64_t now_ms, bool wake_detected,
                                          bool vad_speech) {
  switch (state_) {
    case State::kWaitingForWake:
      if (!wake_detected) return VoiceTurnEvent::kNone;
      // Start streaming at wake detection so the user can speak immediately
      // after the wake word rather than waiting for a second VAD transition.
      state_ = State::kListening;
      listen_started_ms_ = now_ms;
      last_speech_ms_ = now_ms;
      waiting_for_first_speech_ = !vad_speech;
      follow_up_window_ = false;
      manual_capture_ = false;
      return VoiceTurnEvent::kWakeDetected;

    case State::kListening:
      // Manual BOOT capture is an explicit toggle. VAD continues to feed the
      // wake pipeline, but only the second press or RecordingButton's 30-s
      // safety limit may end this capture.
      if (manual_capture_) return VoiceTurnEvent::kNone;
      if (vad_speech) {
        waiting_for_first_speech_ = false;
        last_speech_ms_ = now_ms;
        if (now_ms - listen_started_ms_ < config_.maximum_listen_ms) {
          return VoiceTurnEvent::kNone;
        }
      }
      if (now_ms - listen_started_ms_ >= config_.maximum_listen_ms) {
        reset();
        return VoiceTurnEvent::kListenTimedOut;
      }
      if (waiting_for_first_speech_) {
        if (now_ms - listen_started_ms_ >= config_.wake_to_speech_timeout_ms) {
          const bool follow_up_timed_out = follow_up_window_;
          reset();
          return follow_up_timed_out ? VoiceTurnEvent::kFollowUpTimedOut
                                     : VoiceTurnEvent::kWakeTimedOut;
        }
        return VoiceTurnEvent::kNone;
      }
      if (now_ms - last_speech_ms_ >= config_.endpoint_silence_ms) {
        reset();
        return VoiceTurnEvent::kListenStopped;
      }
      return VoiceTurnEvent::kNone;
  }
  return VoiceTurnEvent::kNone;
}

void VoiceTurnDetector::reset() {
  state_ = State::kWaitingForWake;
  listen_started_ms_ = 0;
  last_speech_ms_ = 0;
  waiting_for_first_speech_ = false;
  follow_up_window_ = false;
  manual_capture_ = false;
}

bool VoiceTurnDetector::listening() const { return state_ == State::kListening; }

}  // namespace sesame::voice
