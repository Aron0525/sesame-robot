#!/usr/bin/env python3
"""Require runtime evidence for every ESP32 TTS downlink boundary."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/sesame_voice/voice_controller.cpp").read_text(
    encoding="utf-8"
)
HEADER = (
    ROOT / "components/sesame_voice/include/sesame_voice/voice_controller.h"
).read_text(encoding="utf-8")


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def main() -> None:
    enqueue = section(
        SOURCE,
        "void VoiceController::enqueue_downlink_packet",
        "void VoiceController::process_control_json",
    )
    playback = section(
        SOURCE,
        "void VoiceController::play_pending_audio",
        "esp_err_t VoiceController::send_control",
    )
    completion = section(
        SOURCE,
        "void VoiceController::complete_tts_if_drained",
        "void VoiceController::fail_tts_playback",
    )

    for field in (
        "downlink_diagnostic_generation_",
        "downlink_received_packet_count_",
        "downlink_max_arrival_gap_ms_",
        "downlink_arrival_gaps_over_25ms_",
        "downlink_queue_high_watermark_",
        "downlink_played_without_buffer_count_",
    ):
        assert field in HEADER

    assert "record_downlink_arrival" in enqueue
    assert "record_downlink_queue_depth" in enqueue
    assert "record_downlink_playout" in playback
    assert "P2 downlink diagnostics" in completion
    print("TTS downlink diagnostics cover arrival, queue, and playout boundaries")


if __name__ == "__main__":
    main()
