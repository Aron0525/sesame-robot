#include "sesame_voice/voice_turn_detector.h"

namespace sesame::voice {

void VoiceTurnDetector::start_collecting(uint64_t now_ms) {
  state_ = VoiceTurnState::kCollectingUserSpeech;
  listen_started_ms_ = now_ms;
  last_speech_ms_ = now_ms;
  consecutive_speech_frames_ = 0;
}

bool VoiceTurnDetector::speech_confirmed(bool vad_speech,
                                         uint8_t required_frames) {
  if (!vad_speech) {
    consecutive_speech_frames_ = 0;
    return false;
  }
  // A zero configuration is treated as one frame, so malformed settings never
  // make speech recognition permanently unavailable.
  const uint8_t required = required_frames == 0 ? 1 : required_frames;
  if (consecutive_speech_frames_ < required) {
    ++consecutive_speech_frames_;
  }
  return consecutive_speech_frames_ >= required;
}

VoiceTurnEvent VoiceTurnDetector::start_from_button(uint64_t now_ms) {
  start_collecting(now_ms);
  return VoiceTurnEvent::kListenStarted;
}

bool VoiceTurnDetector::start_first_speech_wait(uint64_t now_ms) {
  if (state_ != VoiceTurnState::kWakeAcknowledging) return false;
  state_ = VoiceTurnState::kWaitingForFirstSpeech;
  wait_started_ms_ = now_ms;
  return true;
}

bool VoiceTurnDetector::start_tts_playback() {
  if (state_ != VoiceTurnState::kIdleWakeListening) return false;
  state_ = VoiceTurnState::kTtsPlaying;
  return true;
}

bool VoiceTurnDetector::start_followup_wait(uint64_t now_ms) {
  if (state_ != VoiceTurnState::kTtsPlaying) return false;
  // Resetting here discards timestamps from the preceding user utterance.
  state_ = VoiceTurnState::kWaitingForFollowupSpeech;
  wait_started_ms_ = now_ms;
  listen_started_ms_ = 0;
  last_speech_ms_ = 0;
  return true;
}

VoiceTurnEvent VoiceTurnDetector::update(uint64_t now_ms, bool wake_detected,
                                          bool vad_speech) {
  switch (state_) {
    case VoiceTurnState::kIdleWakeListening:
      if (!wake_detected) return VoiceTurnEvent::kNone;
      // Do not begin gateway capture yet: the controller must play the local
      // acknowledgement before opening the first-speech window.
      state_ = VoiceTurnState::kWakeAcknowledging;
      return VoiceTurnEvent::kWakeDetected;

    case VoiceTurnState::kWakeAcknowledging:
      // With AEC intentionally absent, the acknowledgement is microphone-off.
      return VoiceTurnEvent::kNone;

    case VoiceTurnState::kTtsPlaying:
      // The controller will feed this state with AEC-clean VAD frames in the
      // next change. Keeping the transition here makes barge-in deterministic
      // without making raw microphone input interrupt TTS today.
      if (!speech_confirmed(vad_speech, config_.speech_start_frames)) {
        return VoiceTurnEvent::kNone;
      }
      start_collecting(now_ms);
      return VoiceTurnEvent::kBargeInDetected;

    case VoiceTurnState::kWaitingForFirstSpeech:
    case VoiceTurnState::kWaitingForFollowupSpeech: {
      if (speech_confirmed(vad_speech, config_.speech_start_frames)) {
        start_collecting(now_ms);
        return VoiceTurnEvent::kListenStarted;
      }
      if (now_ms - wait_started_ms_ < config_.first_speech_timeout_ms) {
        return VoiceTurnEvent::kNone;
      }
      const VoiceTurnEvent timeout_event =
          state_ == VoiceTurnState::kWaitingForFirstSpeech
              ? VoiceTurnEvent::kWakeTimedOut
              : VoiceTurnEvent::kFollowupTimedOut;
      reset();
      return timeout_event;
    }

    case VoiceTurnState::kCollectingUserSpeech:
      // The ten-second ceiling takes priority even if the last frame contains
      // speech, so a continuously speaking user cannot keep a turn open.
      if (now_ms - listen_started_ms_ >= config_.maximum_listen_ms) {
        reset();
        return VoiceTurnEvent::kListenTimedOut;
      }
      if (speech_confirmed(vad_speech, config_.speech_continue_frames)) {
        last_speech_ms_ = now_ms;
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
  state_ = VoiceTurnState::kIdleWakeListening;
  wait_started_ms_ = 0;
  listen_started_ms_ = 0;
  last_speech_ms_ = 0;
  consecutive_speech_frames_ = 0;
}

bool VoiceTurnDetector::listening() const {
  return state_ == VoiceTurnState::kCollectingUserSpeech;
}

}  // namespace sesame::voice
