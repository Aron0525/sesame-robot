#include <cassert>

#include "sesame_voice/voice_turn_detector.h"

int main() {
  using sesame::voice::VoiceTurnDetector;
  using sesame::voice::VoiceTurnEvent;
  using sesame::voice::VoiceTurnState;

  VoiceTurnDetector detector({
      .first_speech_timeout_ms = 3000,
      .endpoint_silence_ms = 800,
      .maximum_listen_ms = 10000,
      .speech_start_frames = 3,
      .speech_continue_frames = 2,
  });

  // A wake word must first enter acknowledgement state. The controller plays
  // the local “我在” prompt, then begins the three-second first-speech window.
  // Without this acknowledgement a successful local wake is indistinguishable
  // from a missed wake to the person using the device.
  assert(detector.state() == VoiceTurnState::kIdleWakeListening);
  assert(detector.update(0, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(20, true, true) == VoiceTurnEvent::kWakeDetected);
  assert(detector.state() == VoiceTurnState::kWakeAcknowledging);
  assert(!detector.listening());
  assert(detector.start_first_speech_wait(200));
  assert(detector.state() == VoiceTurnState::kWaitingForFirstSpeech);

  assert(detector.update(3199, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(3200, false, false) == VoiceTurnEvent::kWakeTimedOut);
  assert(detector.state() == VoiceTurnState::kIdleWakeListening);

  // A first voice frame opens collection. 0.8 seconds of silence
  // ends collection and hands the captured turn to ASR.
  assert(detector.update(5000, true, false) == VoiceTurnEvent::kWakeDetected);
  assert(detector.start_first_speech_wait(5050));
  // Three consecutive 20-ms VAD speech frames are required. A single noise
  // spike must neither open a first turn nor reset the endpoint timer.
  assert(detector.update(5100, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(5120, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(5140, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(5160, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(5180, false, true) == VoiceTurnEvent::kListenStarted);
  assert(detector.state() == VoiceTurnState::kCollectingUserSpeech);
  assert(detector.update(5880, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(5900, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(6700, false, false) == VoiceTurnEvent::kListenStopped);
  assert(detector.state() == VoiceTurnState::kIdleWakeListening);

  // Recording is bounded by ten seconds even when speech continues.
  assert(detector.update(8000, true, false) == VoiceTurnEvent::kWakeDetected);
  assert(detector.start_first_speech_wait(8050));
  assert(detector.update(8100, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(8120, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(8140, false, true) == VoiceTurnEvent::kListenStarted);
  for (uint64_t timestamp = 8160; timestamp < 18140; timestamp += 20) {
    assert(detector.update(timestamp, false, true) == VoiceTurnEvent::kNone);
  }
  assert(detector.update(18140, false, true) == VoiceTurnEvent::kListenTimedOut);

  // Only the wake word interrupts a reply; ordinary VAD activity must not.
  assert(detector.start_tts_playback());
  assert(detector.state() == VoiceTurnState::kTtsPlaying);
  assert(detector.update(20000, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(20020, true, false) == VoiceTurnEvent::kBargeInDetected);
  assert(detector.state() == VoiceTurnState::kCollectingUserSpeech);

  // A completed reply opens the same three-second first-speech gate without
  // requiring another wake word. Three VAD frames start a follow-up turn.
  detector.reset();
  assert(detector.start_tts_playback());
  assert(detector.start_followup_wait(22000));
  assert(detector.state() == VoiceTurnState::kWaitingForFollowupSpeech);
  assert(detector.update(24999, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(25000, false, false) ==
         VoiceTurnEvent::kFollowupTimedOut);
  assert(detector.state() == VoiceTurnState::kIdleWakeListening);

  assert(detector.start_tts_playback());
  assert(detector.start_followup_wait(26000));
  assert(detector.update(26100, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(26120, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(26140, false, true) == VoiceTurnEvent::kListenStarted);
  assert(detector.listening());

  // BOOT keeps its existing direct manual-recording behavior.
  detector.reset();
  assert(detector.start_from_button(30000) == VoiceTurnEvent::kListenStarted);
  assert(detector.listening());
}
