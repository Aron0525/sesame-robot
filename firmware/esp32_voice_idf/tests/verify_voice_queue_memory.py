#!/usr/bin/env python3
"""Keep callback synchronization in a small internal-RAM budget for TLS."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/sesame_voice/voice_controller.cpp"
HEADER = ROOT / "components/sesame_voice/include/sesame_voice/voice_controller.h"
GATEWAY_SOURCE = ROOT / "components/sesame_transport/gateway_client.cpp"
GATEWAY_HEADER = ROOT / "components/sesame_transport/include/sesame_transport/gateway_client.h"


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    gateway_source = GATEWAY_SOURCE.read_text(encoding="utf-8")
    gateway_header = GATEWAY_HEADER.read_text(encoding="utf-8")
    compact = " ".join(source.split())
    assert "#include \"freertos/idf_additions.h\"" in source
    assert "xQueueCreateWithCaps(" not in source
    assert "xQueueCreate(kGatewayEventQueueDepth, sizeof(GatewayEvent))" in compact
    assert "xQueueCreate(kGatewayEventQueueDepth, sizeof(uint8_t))" not in compact
    assert "heap_caps_malloc( kGatewayEventQueueDepth * kMaxGatewayEventBytes" not in compact
    assert "gateway_event_data_" not in source
    assert "gateway_event_free_queue_" not in source
    assert "kGatewayEventQueueDepth = 4" in header
    assert "GatewayEvent gateway_event_work_" in header
    assert "DownlinkPacket downlink_work_packet_" in header
    enqueue_control = source[
        source.index("void VoiceController::enqueue_gateway_event") : source.index(
            "void VoiceController::process_gateway_events"
        )
    ]
    enqueue_audio = source[
        source.index("void VoiceController::enqueue_downlink_packet") : source.index(
            "void VoiceController::process_control_json"
        )
    ]
    assert "GatewayEvent event{}" not in enqueue_control
    assert "DownlinkPacket packet{" not in enqueue_audio
    assert "GatewayEvent& event = gateway_event_work_" in enqueue_control
    assert "DownlinkPacket& packet = downlink_work_packet_" in enqueue_audio
    # mbedTLS is configured for internal-RAM allocation. Do not consume an
    # extra 8 KiB here: hardware verification showed that doing so starves the
    # certificate handshake. Removing the 2.5-KiB callback locals is the safe
    # way to recover event-task stack headroom.
    assert "kWebsocketTaskStackBytes = 8192" in gateway_header
    assert "websocket_config.task_stack = kWebsocketTaskStackBytes" in gateway_source
    assert "WSS event task stack free=" in gateway_source
    assert "vQueueDelete(gateway_event_queue_)" in source
    print("Gateway callback queue stays within the TLS-safe internal RAM budget")


if __name__ == "__main__":
    main()
