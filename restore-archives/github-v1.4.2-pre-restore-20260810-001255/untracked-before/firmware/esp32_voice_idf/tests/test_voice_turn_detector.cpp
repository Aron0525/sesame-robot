#include <cassert>

#include "sesame_voice/voice_turn_detector.h"

int main() {
  using sesame::voice::VoiceTurnDetector;
  using sesame::voice::VoiceTurnEvent;
  using sesame::voice::VoiceTurnState;

  VoiceTurnDetector detector({
      .first_speech_timeout_ms = 3000,
      .endpoint_silence_ms = 1000,
      .maximum_listen_ms = 10000,
      .speech_start_frames = 3,
      .speech_continue_frames = 2,
  });

  // Idle wake listening -> "我在" playing. Speech is ignored until the
  // acknowledgement audio has completed, because AEC is deliberately absent.
  assert(detector.state() == VoiceTurnState::kIdleWakeListening);
  assert(detector.update(0, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(20, true, true) == VoiceTurnEvent::kWakeDetected);
  assert(detector.state() == VoiceTurnState::kWakeAcknowledging);
  assert(!detector.listening());
  assert(detector.update(40, false, true) == VoiceTurnEvent::kNone);

  // The three-second first-utterance window starts after "我在" completes.
  detector.start_first_speech_wait(400);
  assert(detector.state() == VoiceTurnState::kWaitingForFirstSpeech);
  assert(detector.update(3399, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(3400, false, false) == VoiceTurnEvent::kWakeTimedOut);
  assert(detector.state() == VoiceTurnState::kIdleWakeListening);

  // A first voice frame opens collection. One continuous second of silence
  // ends collection and hands the captured turn to ASR.
  assert(detector.update(5000, true, false) == VoiceTurnEvent::kWakeDetected);
  detector.start_first_speech_wait(5300);
  // Three consecutive 20-ms VAD speech frames are required. A single noise
  // spike must neither open a first turn nor reset the one-second endpoint.
  assert(detector.update(5600, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(5620, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(5640, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(5660, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(5680, false, true) == VoiceTurnEvent::kListenStarted);
  assert(detector.state() == VoiceTurnState::kCollectingUserSpeech);
  assert(detector.update(6580, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(6600, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(6680, false, false) == VoiceTurnEvent::kListenStopped);
  assert(detector.state() == VoiceTurnState::kIdleWakeListening);

  // Recording is bounded by ten seconds even when speech continues.
  assert(detector.update(8000, true, false) == VoiceTurnEvent::kWakeDetected);
  detector.start_first_speech_wait(8300);
  assert(detector.update(8500, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(8520, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(8540, false, true) == VoiceTurnEvent::kListenStarted);
  for (uint64_t timestamp = 8560; timestamp < 18540; timestamp += 20) {
    assert(detector.update(timestamp, false, true) == VoiceTurnEvent::kNone);
  }
  assert(detector.update(18540, false, true) == VoiceTurnEvent::kListenTimedOut);

  // The future AEC path will feed TTS playback into this barge-in branch.
  // It is state-tested now, while the controller deliberately leaves it
  // unwired until echo cancellation is available.
  assert(detector.start_tts_playback());
  assert(detector.state() == VoiceTurnState::kTtsPlaying);
  assert(detector.update(20000, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(20020, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(20040, false, true) == VoiceTurnEvent::kBargeInDetected);
  assert(detector.state() == VoiceTurnState::kCollectingUserSpeech);

  // A completed remote TTS opens the same three-second follow-up window.
  detector.reset();
  assert(detector.start_tts_playback());
  assert(detector.start_followup_wait(21000));
  assert(detector.state() == VoiceTurnState::kWaitingForFollowupSpeech);
  assert(detector.update(23999, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(24000, false, false) == VoiceTurnEvent::kFollowupTimedOut);

  assert(detector.start_tts_playback());
  assert(detector.start_followup_wait(25000));
  assert(detector.update(25100, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(25120, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(25140, false, true) == VoiceTurnEvent::kListenStarted);
  assert(detector.state() == VoiceTurnState::kCollectingUserSpeech);

  // BOOT keeps its existing direct manual-recording behavior.
  detector.reset();
  assert(detector.start_from_button(30000) == VoiceTurnEvent::kListenStarted);
  assert(detector.listening());
}
