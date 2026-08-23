#include <cassert>

#include "sesame_voice/voice_turn_detector.h"

int main() {
  using sesame::voice::VoiceTurnDetector;
  using sesame::voice::VoiceTurnEvent;

  VoiceTurnDetector detector({
      .wake_to_speech_timeout_ms = 3000,
      .endpoint_silence_ms = 800,
      .maximum_listen_ms = 10000,
  });

  assert(detector.update(0, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(20, true, false) == VoiceTurnEvent::kWakeDetected);
  assert(detector.listening());  // Start streaming immediately after the wake word.
  assert(detector.update(40, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(500, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(840, false, false) == VoiceTurnEvent::kListenStopped);
  assert(!detector.listening());

  detector.reset();
  assert(detector.update(1000, true, false) == VoiceTurnEvent::kWakeDetected);
  assert(detector.update(4000, false, false) == VoiceTurnEvent::kWakeTimedOut);

  detector.reset();
  assert(detector.update(5000, true, true) == VoiceTurnEvent::kWakeDetected);
  assert(detector.listening());
  assert(detector.update(5020, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(10020, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(15020, false, true) == VoiceTurnEvent::kListenTimedOut);

  detector.reset();
  assert(detector.start_from_button(16000) == VoiceTurnEvent::kListenStarted);
  assert(detector.listening());

  detector.reset();
  assert(detector.start_follow_up(20000) == VoiceTurnEvent::kListenStarted);
  assert(detector.listening());
  assert(detector.update(22999, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(23000, false, false) ==
         VoiceTurnEvent::kFollowUpTimedOut);
  assert(!detector.listening());

  assert(detector.start_follow_up(24000) == VoiceTurnEvent::kListenStarted);
  assert(detector.update(24500, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(25299, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(25300, false, false) ==
         VoiceTurnEvent::kListenStopped);
}
