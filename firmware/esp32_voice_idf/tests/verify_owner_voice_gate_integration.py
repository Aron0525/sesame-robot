#!/usr/bin/env python3
"""Protect the intended wiring: phrase match plus owner gate, BOOT bypass."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTROLLER = (ROOT / "components/sesame_voice/voice_controller.cpp").read_text()
HEADER = (ROOT / "components/sesame_voice/include/sesame_voice/voice_controller.h").read_text()
CMAKE = (ROOT / "components/sesame_voice/CMakeLists.txt").read_text()

assert '"owner_voiceprint_template.h"' in CONTROLLER
assert "OwnerVoiceGate* owner_voice_gate_" in HEADER
assert '"owner_voice_gate.cpp"' in CMAKE

# Spoken wake is accepted only after the owner template passes. This assertion
# deliberately scopes to the wake branch, so a separate BOOT path remains a
# manual accessibility/control mechanism.
wake_branch = CONTROLLER.split("if (event == VoiceTurnEvent::kWakeDetected)", 1)[1]
wake_branch = wake_branch.split("} else if (event == VoiceTurnEvent::kListenStarted)", 1)[0]
assert "owner_voice_gate_->verify_latest()" in wake_branch
assert "decision.accepted" in wake_branch
assert "begin_wake_ack();" in wake_branch
assert wake_branch.index("decision.accepted") < wake_branch.index("begin_wake_ack();")

button_branch = CONTROLLER.split("void VoiceController::handle_button", 1)[1]
button_branch = button_branch.split("void VoiceController::begin_wake_ack", 1)[0]
assert "start_listening(timestamp, CaptureSource::kManual);" in button_branch
assert "verify_latest" not in button_branch
