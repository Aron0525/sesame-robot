from __future__ import annotations

import asyncio
import json
import unittest
from unittest.mock import patch

from sesame_voice_gateway.app import (
    DeviceSession,
    _DownlinkTiming,
    _begin_downlink_flow,
    _send_stream_outputs,
)
from sesame_voice_gateway.protocol.audio import unpack_audio_frame
from sesame_voice_gateway.protocol.control import parse_control_event
from sesame_voice_gateway.streaming import StreamAudio, StreamFinished, StreamStart


class _WebSocket:
    def __init__(self) -> None:
        self.text_frames: list[str] = []
        self.binary_frames: list[bytes] = []

    async def send_text(self, value: str) -> None:
        self.text_frames.append(value)

    async def send_bytes(self, value: bytes) -> None:
        self.binary_frames.append(value)


async def _outputs():
    yield StreamStart(generation_id=7)
    yield StreamAudio(sentence="第一句。", opus_packets=(b"opus-a", b"opus-b"))
    yield StreamFinished(expression="happy")


class StreamingDownlinkTest(unittest.IsolatedAsyncioTestCase):
    async def test_downlink_timing_reports_drift_and_late_intervals(self) -> None:
        timing = _DownlinkTiming()
        timing.record_packet_sent(10.000)
        timing.record_packet_sent(10.021)
        timing.record_packet_sent(10.044)

        self.assertEqual(
            timing.as_details(),
            {
                "packet_count": 3,
                "elapsed_ms": 44,
                "expected_elapsed_ms": 40,
                "drift_ms": 4,
                "cumulative_drift_ms": 4,
                "last_interval_ms": 23,
                "average_interval_ms": 22,
                "max_interval_ms": 23,
                "max_lateness_ms": 4,
            },
        )

    async def test_v1_device_receives_safe_plan_then_one_continuous_opus_stream(self) -> None:
        websocket = _WebSocket()
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        delays: list[float] = []

        async def record_delay(seconds: float) -> None:
            delays.append(seconds)

        with patch("sesame_voice_gateway.app.asyncio.sleep", new=record_delay):
            await _send_stream_outputs(websocket, session, _outputs(), turn_id="turn_001")

        controls = [json.loads(frame) for frame in websocket.text_frames]
        parsed_controls = [parse_control_event(frame) for frame in websocket.text_frames]
        self.assertEqual([event["type"] for event in controls], ["response.plan", "tts.start", "tts.stop"])
        self.assertEqual([event.type for event in parsed_controls], ["response.plan", "tts.start", "tts.stop"])
        self.assertEqual(controls[0]["payload"]["expression_id"], "idle")
        self.assertIsNone(controls[0]["payload"]["action_id"])
        # The first bootstrap packets are intentionally sent without a
        # relative 20-ms sleep so ESP32 can fill its 600-ms jitter buffer.
        # After the watermark telemetry arrives, flow control regulates them.
        self.assertEqual(delays, [])
        packets = [unpack_audio_frame(frame) for frame in websocket.binary_frames]
        self.assertEqual([packet.sequence for packet in packets], [0, 1])
        self.assertEqual([packet.generation_id for packet in packets], [7, 7])

    async def test_gateway_pauses_at_high_watermark_then_refills_after_low_watermark(self) -> None:
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        flow = _begin_downlink_flow(
            session, generation_id=7, turn_id="turn_001", fixed_music=False
        )
        self.assertEqual(flow.bootstrap_packets, 40)
        flow.packets_sent = 40
        # The first telemetry sample can be stale while the WSS bootstrap is
        # still in flight. It must not authorize an extra burst.
        session.playback_stats = {
            "generation_id": 7,
            "buffered_packets": 4,
            "max_buffered_packets": 4,
            "low_watermark_packets": 20,
            "high_watermark_packets": 40,
        }
        session.playback_stats_revision = 1
        flow._refresh(session)
        self.assertFalse(flow.bootstrap_confirmed)
        self.assertEqual(flow._send_credit, 0)
        session.playback_stats = {
            "generation_id": 7,
            "buffered_packets": 40,
            "max_buffered_packets": 40,
            "low_watermark_packets": 20,
            "high_watermark_packets": 40,
        }
        session.playback_stats_revision = 2
        flow._refresh(session)
        self.assertEqual(flow._send_credit, 0)

        waiting = asyncio.create_task(
            flow.wait_before_send(session, fallback_deadline=0.0)
        )
        await asyncio.sleep(0)
        self.assertFalse(waiting.done())
        session.playback_stats = {
            "generation_id": 7,
            "buffered_packets": 19,
            "max_buffered_packets": 40,
            "low_watermark_packets": 20,
            "high_watermark_packets": 40,
        }
        session.playback_stats_revision = 3
        session.playback_stats_event.set()
        await asyncio.wait_for(waiting, timeout=0.1)
        self.assertEqual(flow._send_credit, 10)
        flow.record_sent()
        self.assertEqual(flow._send_credit, 9)

    async def test_pause_holds_the_sender_until_resume(self) -> None:
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        flow = _begin_downlink_flow(
            session, generation_id=7, turn_id="test_001", fixed_music=True
        )
        session.playback_paused = True
        waiting = asyncio.create_task(
            flow.wait_before_send(session, fallback_deadline=0.0)
        )
        await asyncio.sleep(0)
        self.assertFalse(waiting.done())

        session.playback_paused = False
        session.playback_pause_event.set()
        await asyncio.wait_for(waiting, timeout=0.1)


if __name__ == "__main__":
    unittest.main()
