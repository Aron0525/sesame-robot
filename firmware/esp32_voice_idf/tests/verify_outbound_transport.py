#!/usr/bin/env python3
"""Verify that real-time capture never performs a blocking WSS write inline."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTROLLER = ROOT / "components/sesame_voice/voice_controller.cpp"
HEADER = ROOT / "components/sesame_voice/include/sesame_voice/voice_controller.h"


def section(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def main() -> None:
    source = CONTROLLER.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    compact_source = " ".join(source.split())
    capture = section(source, "void VoiceController::capture_and_send", "void VoiceController::play_pending_audio")
    assert "enqueue_outbound_binary" in capture
    assert "gateway_.send_binary" not in capture
    assert "outbound_queue_" in source
    assert "void VoiceController::outbound_loop" in source
    outbound = section(source, "void VoiceController::outbound_loop", "void VoiceController::run")
    assert "gateway_.send_binary" in outbound
    assert "gateway_.send_text" in outbound
    # ESP-TLS has a deep call chain. Keep the work item in static controller
    # storage and retain the sender stack budget that survives real upload.
    assert "kOutboundTaskStackBytes = 24576" in header
    assert "OutboundFrame outbound_work_frame_" in header
    assert "OutboundFrame& frame = outbound_work_frame_" in outbound
    assert "OutboundFrame frame{}" not in outbound
    assert "frame.size, 2000" in outbound
    assert "transport_fault_requested_ = true" in outbound
    assert '"sesame_uplink", kOutboundTaskStackBytes' in compact_source
    sdkconfig = (ROOT / "sdkconfig").read_text(encoding="utf-8")
    assert "CONFIG_ESP_WS_CLIENT_SEPARATE_TX_LOCK=y" in sdkconfig
    assert "CONFIG_ESP_WS_CLIENT_TX_LOCK_TIMEOUT_MS=2000" in sdkconfig
    print("Non-blocking outbound transport architecture verified")


if __name__ == "__main__":
    main()
