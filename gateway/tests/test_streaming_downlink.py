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
    _wait_for_playback_complete,
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
    async def test_gateway_waits_for_the_device_render_completion_ack(self) -> None:
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        session.playback_stats_generation = 7
        session.playback_stats_revision = 1
        session.playback_stats = {
            "generation_id": 7,
            "playback_complete": False,
            "rendered_frames": 20,
        }
        waiting = asyncio.create_task(
            _wait_for_playback_complete(
                session,
                generation_id=7,
                expected_frames=25,
                timeout_seconds=0.1,
            )
        )
        await asyncio.sleep(0)
        self.assertFalse(waiting.done())

        session.playback_stats = {
            "generation_id": 7,
            "playback_complete": True,
            "rendered_frames": 25,
            "underflow_count": 0,
            "dropped_packet_count": 0,
            "stale_generation_count": 0,
            "out_of_order_count": 0,
            "sequence_discontinuity_count": 0,
        }
        session.playback_stats_revision += 1
        session.playback_stats_event.set()
        self.assertTrue(await waiting)

    async def test_gateway_rejects_a_false_playback_completion(self) -> None:
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        session.playback_stats_generation = 7
        session.playback_stats = {
            "generation_id": 7,
            "playback_complete": True,
            "rendered_frames": 0,
            "underflow_count": 0,
            "dropped_packet_count": 30,
            "stale_generation_count": 0,
            "out_of_order_count": 0,
            "sequence_discontinuity_count": 0,
        }
        self.assertFalse(
            await _wait_for_playback_complete(
                session,
                generation_id=7,
                expected_frames=30,
                timeout_seconds=0.0,
            )
        )

    async def test_short_audio_waits_for_its_first_completion_stats(self) -> None:
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        session.playback_stats_generation = 7
        waiting = asyncio.create_task(
            _wait_for_playback_complete(
                session,
                generation_id=7,
                expected_frames=2,
                timeout_seconds=0.1,
            )
        )
        await asyncio.sleep(0)
        self.assertFalse(waiting.done())
        session.playback_stats = {
            "generation_id": 7,
            "playback_complete": True,
            "rendered_frames": 2,
            "underflow_count": 0,
            "dropped_packet_count": 0,
            "stale_generation_count": 0,
            "out_of_order_count": 0,
            "sequence_discontinuity_count": 0,
        }
        session.playback_stats_event.set()
        self.assertTrue(await waiting)

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

    async def test_gateway_bootstrap_matches_esp32_start_then_refills_below_high_watermark(self) -> None:
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        flow = _begin_downlink_flow(
            session, generation_id=7, turn_id="turn_001", fixed_music=False
        )
        self.assertEqual(flow.bootstrap_packets, 30)
        flow.packets_sent = 30
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
        # The renderer can dequeue the 30th packet before the receive callback
        # samples queue depth. A started renderer is equivalent proof that the
        # 30-frame startup threshold was reached.
        session.playback_stats = {
            "generation_id": 7,
            "buffered_packets": 29,
            "max_buffered_packets": 29,
            "low_watermark_packets": 20,
            "high_watermark_packets": 40,
            "playback_started": True,
        }
        session.playback_stats_revision = 2
        flow._refresh(session)
        self.assertTrue(flow.bootstrap_confirmed)
        self.assertEqual(flow._send_credit, 5)
        session.playback_stats = {
            "generation_id": 7,
            "buffered_packets": 30,
            "max_buffered_packets": 30,
            "low_watermark_packets": 20,
            "high_watermark_packets": 40,
        }
        session.playback_stats_revision = 3
        flow._refresh(session)
        self.assertTrue(flow.bootstrap_confirmed)
        self.assertEqual(flow._send_credit, 5)
        await asyncio.wait_for(
            flow.wait_before_send(session, fallback_deadline=0.0), timeout=0.1
        )
        flow.record_sent()
        self.assertEqual(flow._send_credit, 4)

        flow._send_credit = 0
        session.playback_stats = {
            "generation_id": 7,
            "buffered_packets": 19,
            "max_buffered_packets": 40,
            "low_watermark_packets": 20,
            "high_watermark_packets": 40,
        }
        session.playback_stats_revision = 4
        session.playback_stats_event.set()
        waiting = asyncio.create_task(
            flow.wait_before_send(session, fallback_deadline=0.0)
        )
        await asyncio.wait_for(waiting, timeout=0.1)
        self.assertEqual(flow._send_credit, 10)
        flow.record_sent()
        self.assertEqual(flow._send_credit, 9)

    async def test_confirmed_playback_credit_does_not_bypass_20ms_timeline(self) -> None:
        """Watermark credit authorizes a packet, but never an early burst."""
        clock = type("Clock", (), {"now": 0.0})()
        delays: list[float] = []

        def perf_counter() -> float:
            return clock.now

        async def sleep(seconds: float) -> None:
            delays.append(seconds)
            clock.now += seconds

        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        flow = _begin_downlink_flow(
            session, generation_id=7, turn_id="turn_001", fixed_music=False
        )
        flow.packets_sent = flow.bootstrap_packets
        session.playback_stats = {
            "generation_id": 7,
            "buffered_packets": 20,
            "max_buffered_packets": 30,
            "low_watermark_packets": 20,
            "high_watermark_packets": 40,
            "playback_started": True,
        }
        session.playback_stats_revision = 1

        with (
            patch("sesame_voice_gateway.app.time.perf_counter", new=perf_counter),
            patch("sesame_voice_gateway.app.asyncio.sleep", new=sleep),
        ):
            await flow.wait_before_send(session, fallback_deadline=0.6)

        self.assertEqual(delays, [0.6])

    async def test_confirmed_playback_credit_spaces_successive_refill_packets(self) -> None:
        """A granted refill burst still traverses the media clock one frame at a time."""
        clock = type("Clock", (), {"now": 0.0})()
        delays: list[float] = []

        def perf_counter() -> float:
            return clock.now

        async def sleep(seconds: float) -> None:
            delays.append(seconds)
            clock.now += seconds

        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        flow = _begin_downlink_flow(
            session, generation_id=7, turn_id="turn_001", fixed_music=False
        )
        flow.packets_sent = flow.bootstrap_packets
        session.playback_stats = {
            "generation_id": 7,
            "buffered_packets": 20,
            "max_buffered_packets": 30,
            "low_watermark_packets": 20,
            "high_watermark_packets": 40,
            "playback_started": True,
        }
        session.playback_stats_revision = 1

        with (
            patch("sesame_voice_gateway.app.time.perf_counter", new=perf_counter),
            patch("sesame_voice_gateway.app.asyncio.sleep", new=sleep),
        ):
            await flow.wait_before_send(session, fallback_deadline=0.0)
            flow.record_sent()
            await flow.wait_before_send(session, fallback_deadline=0.0)

        self.assertEqual(delays, [0.02])

    async def test_legacy_device_waits_for_telemetry_only_once(self) -> None:
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        flow = _begin_downlink_flow(
            session, generation_id=7, turn_id="turn_001", fixed_music=False
        )
        flow.packets_sent = flow.bootstrap_packets
        wait_count = 0

        async def timeout_once(awaitable, *, timeout: float):
            nonlocal wait_count
            wait_count += 1
            awaitable.close()
            raise TimeoutError

        with patch(
            "sesame_voice_gateway.app.asyncio.wait_for",
            new=timeout_once,
        ):
            await flow.wait_before_send(session, fallback_deadline=0.0)
            await flow.wait_before_send(session, fallback_deadline=0.0)

        self.assertEqual(wait_count, 1)

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
        self.assertEqual(flow.bootstrap_packets, 30)
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
