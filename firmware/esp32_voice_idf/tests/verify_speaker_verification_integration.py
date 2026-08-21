#!/usr/bin/env python3
"""Protect optional speaker verification plus the independent BOOT path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTROLLER = (ROOT / "components/sesame_voice/voice_controller.cpp").read_text()
HEADER = (ROOT / "components/sesame_voice/include/sesame_voice/voice_controller.h").read_text()
CMAKE = (ROOT / "components/sesame_voice/CMakeLists.txt").read_text()
STORE = (ROOT / "components/sesame_voice/speaker_verification_store.cpp").read_text()

assert '"speaker_verification_template.h"' in CONTROLLER
assert "SpeakerVerification* speaker_verification_" in HEADER
assert "speaker_verification_enabled_" in HEADER
assert '"speaker_verification.cpp"' in CMAKE
assert '"speaker_verification_store.cpp"' in CMAKE

# The NVS write is delegated to its internal-RAM task; the PSRAM voice task
# only updates live state and enqueues the persistence request.
assert "save_speaker_verification_enabled" not in CONTROLLER
assert "speaker_verification_store_.enqueue(enabled)" in CONTROLLER
assert "xTaskCreatePinnedToCore" in STORE
assert "save_speaker_verification_enabled" in STORE

# A MultiNet phrase match is gated only when the persisted feature switch is
# enabled. BOOT remains an independent manual accessibility/control mechanism.
wake_branch = CONTROLLER.split("if (event == VoiceTurnEvent::kWakeDetected)", 1)[1]
wake_branch = wake_branch.split("} else if (event == VoiceTurnEvent::kListenStarted)", 1)[0]
assert "begin_wake_ack();" in wake_branch
assert "speaker_verification_enabled_" in wake_branch
assert "verify_latest" in wake_branch
assert wake_branch.index("verify_latest") < wake_branch.index("begin_wake_ack();")

button_branch = CONTROLLER.split("void VoiceController::handle_button", 1)[1]
button_branch = button_branch.split("void VoiceController::begin_wake_ack", 1)[0]
assert "start_listening(timestamp, CaptureSource::kManual);" in button_branch
assert "verify_latest" not in button_branch
