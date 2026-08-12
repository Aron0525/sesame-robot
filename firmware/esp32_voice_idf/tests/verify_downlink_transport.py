#!/usr/bin/env python3
"""Keep streamed TTS audio from starving ordered control callbacks."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/sesame_voice/voice_controller.cpp"
HEADER = ROOT / "components/sesame_voice/include/sesame_voice/voice_controller.h"


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    callback = section(
        source,
        "void VoiceController::on_gateway_binary",
        "void VoiceController::enqueue_downlink_packet",
    )
    playback = section(
        source,
        "void VoiceController::play_pending_audio",
        "esp_err_t VoiceController::send_control",
    )
    completion = section(
        source,
        "void VoiceController::complete_tts_if_drained",
        "void VoiceController::fail_tts_playback",
    )

    # Binary TTS frames have their own compact queue. They must never occupy
    # the large ordered-control queue and crowd out tts.stop/interrupt events.
    assert "enqueue_downlink_packet(data, size)" in callback
    assert "enqueue_gateway_event" not in callback
    gateway_event_kinds = section(
        header, "enum class GatewayEventKind", "struct GatewayEvent"
    )
    assert "kBinary" not in gateway_event_kinds
    assert "downlink_fault_requested_" in header
    assert "downlink_fault_requested_ = true" in source

    # All generation and sequence state remains owned by the voice task.
    assert "packet.sequence != expected_downlink_sequence_" in playback
    assert "packet.generation_id != active_generation_" in playback

    # TTS keeps microphone input active only so a full MultiNet wake phrase can
    # interrupt playback; raw VAD is ignored in kTtsPlaying.
    run = section(
        source,
        "void VoiceController::run()",
        "void VoiceController::maintain_gateway_connection",
    )
    assert "else if (session_ready_)" in run
    assert "VoiceTurnState::kTtsPlaying" in run
    assert "tts_barge_in" in run
    # A finished generation is historical state, not an active interrupt
    # target. Leaving it non-zero misbinds a BOOT interrupt during thinking.
    assert "active_generation_ = 0" in completion
    print("TTS audio uses a dedicated queue without starving control events")


if __name__ == "__main__":
    main()
