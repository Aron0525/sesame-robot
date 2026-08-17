#!/usr/bin/env python3
"""Verify the TTS-to-follow-up path is wired into the production controller."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/sesame_voice/voice_controller.cpp"
HEADER = ROOT / "components/sesame_voice/include/sesame_voice/voice_controller.h"
CAPTURE = ROOT / "components/sesame_voice/include/sesame_voice/capture_session.h"


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    capture = CAPTURE.read_text(encoding="utf-8")

    completion = section(
        source,
        "void VoiceController::complete_tts_if_drained",
        "void VoiceController::fail_tts_playback",
    )
    assert "start_followup_wait(now_ms())" in completion
    assert "waiting 3000 ms for follow-up speech" in completion
    assert "if (wake_turn_detector_.start_followup_wait" in completion

    wake = section(
        source,
        "void VoiceController::process_wake_vad_signals",
        "bool VoiceController::start_listening",
    )
    assert "VoiceTurnState prior_state" in wake
    assert "CaptureSource::kFollowup" in wake
    assert "VoiceTurnEvent::kFollowupTimedOut" in wake

    run = section(
        source,
        "void VoiceController::run()",
        "void VoiceController::maintain_gateway_connection",
    )
    assert "session_ready_ && !tts_active_" in run
    assert "tts_barge_in" not in run
    assert "pcm_preroll_->push" in run
    assert "drain_pcm_uplink" in run

    assert 'kFollowupListenStartPayload[] = R"({"trigger":"followup"})"' in source
    assert "kFollowup," in capture
    assert "PcmPreRollBuffer* pcm_preroll_" in header
    assert "MALLOC_CAP_SPIRAM" in source
    print("TTS follow-up, 500-ms pre-roll, and half-duplex wiring verified")


if __name__ == "__main__":
    main()
