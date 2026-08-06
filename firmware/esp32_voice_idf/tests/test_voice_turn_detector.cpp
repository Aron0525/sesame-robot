#include <cassert>

#include "sesame_voice/voice_turn_detector.h"

using sesame::voice::VoiceTurnDetector;
using sesame::voice::VoiceTurnDetectorConfig;
using sesame::voice::VoiceTurnEvent;
using sesame::voice::kWakeVoiceTurnConfig;

int main() {
  static_assert(kWakeVoiceTurnConfig.wake_to_speech_timeout_ms == 3000);
  static_assert(kWakeVoiceTurnConfig.minimum_listen_ms == 0);
  static_assert(kWakeVoiceTurnConfig.endpoint_silence_ms == 2000);
  static_assert(kWakeVoiceTurnConfig.maximum_listen_ms == 30000);
  VoiceTurnDetector detector(kWakeVoiceTurnConfig);

  assert(detector.update(100, true, false) == VoiceTurnEvent::kWakeDetected);
  assert(!detector.listening());
  assert(detector.update(220, false, true) == VoiceTurnEvent::kListenStarted);
  assert(detector.listening());
  assert(detector.update(700, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(2219, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(2220, false, false) == VoiceTurnEvent::kListenStopped);
  assert(!detector.listening());

  assert(detector.update(2000, true, false) == VoiceTurnEvent::kWakeDetected);
  assert(detector.update(5000, false, false) == VoiceTurnEvent::kWakeTimedOut);
  assert(!detector.listening());

  detector.begin_waiting_for_speech(6000);
  assert(detector.update(8999, false, false) == VoiceTurnEvent::kNone);
  assert(detector.update(9000, false, false) == VoiceTurnEvent::kWakeTimedOut);

  assert(detector.start_listening(10000) == VoiceTurnEvent::kListenStarted);
  assert(detector.update(39999, false, true) == VoiceTurnEvent::kNone);
  assert(detector.update(40000, false, true) == VoiceTurnEvent::kListenTimedOut);
  assert(!detector.listening());
}
