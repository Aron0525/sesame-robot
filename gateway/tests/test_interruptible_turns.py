from __future__ import annotations

import asyncio
import json
import unittest
from unittest.mock import patch

from sesame_voice_gateway.app import (
    DeviceSession,
    _handle_control_event,
    _send_turn_result,
)
from sesame_voice_gateway.pipeline import TurnResult
from sesame_voice_gateway.protocol.control import ControlEvent, parse_control_event
from sesame_voice_gateway.providers.base import (
    AgentResult,
    AsrResult,
    ExpressionSpec,
    VoiceSpec,
)


class FakeWebSocket:
    def __init__(self) -> None:
        self.text_frames: list[str] = []
        self.binary_frames: list[bytes] = []

    async def send_text(self, value: str) -> None:
        self.text_frames.append(value)

    async def send_bytes(self, value: bytes) -> None:
        self.binary_frames.append(value)


class _Clock:
    def __init__(self) -> None:
        self.now = 0.0
        self.sleep_calls: list[float] = []

    def perf_counter(self) -> float:
        return self.now

    async def sleep(self, seconds: float) -> None:
        self.sleep_calls.append(seconds)
        self.now += seconds


class ClockedWebSocket(FakeWebSocket):
    def __init__(self, clock: _Clock, send_cost_seconds: float) -> None:
        super().__init__()
        self._clock = clock
        self._send_cost_seconds = send_cost_seconds
        self.packet_sent_at: list[float] = []

    async def send_bytes(self, value: bytes) -> None:
        self.packet_sent_at.append(self._clock.now)
        await super().send_bytes(value)
        self._clock.now += self._send_cost_seconds


class BlockingPipeline:
    def __init__(self) -> None:
        self.started = asyncio.Event()
        self.cancelled = asyncio.Event()

    async def process_turn(self, **_: object) -> TurnResult:
        self.started.set()
        try:
            await asyncio.Event().wait()
        except asyncio.CancelledError:
            self.cancelled.set()
            raise
        raise AssertionError("blocking turn unexpectedly completed")


def _event(event_type: str, *, turn_id: str | None, sequence: int) -> ControlEvent:
    return ControlEvent(
        v=1,
        type=event_type,  # type: ignore[arg-type]
        session_id="ses_001",
        turn_id=turn_id,
        request_id=None,
        sequence=sequence,
        timestamp_ms=1_000,
        payload={},
    )


class InterruptibleTurnTest(unittest.IsolatedAsyncioTestCase):
    async def test_interrupt_cancels_processing_turn_and_flushes_device(self) -> None:
        websocket = FakeWebSocket()
        pipeline = BlockingPipeline()
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
            is_listening=True,
            turn_id="turn_001",
            opus_packets=[b"one-opus-packet"],
        )

        await _handle_control_event(
            websocket, session, pipeline, _event("listen.stop", turn_id="turn_001", sequence=1)
        )
        await asyncio.wait_for(pipeline.started.wait(), timeout=1)
        await _handle_control_event(websocket, session, pipeline, _event("interrupt", turn_id=None, sequence=2))
        await asyncio.wait_for(pipeline.cancelled.wait(), timeout=1)

        controls = [parse_control_event(frame) for frame in websocket.text_frames]
        self.assertEqual([event.type for event in controls], ["tts.flush"])
        self.assertIsNone(session.active_turn_task)

    async def test_downlink_bootstrap_fills_the_esp32_jitter_buffer_without_relative_sleep(self) -> None:
        clock = _Clock()
        websocket = ClockedWebSocket(clock, send_cost_seconds=0.0)
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
            turn_id="turn_001",
        )
        result = TurnResult(
            transcript=AsrResult(text="测试"),
            agent=AgentResult(
                text="回复",
                voice=VoiceSpec(),
                expression=ExpressionSpec(name="happy", ttl_ms=1_000),
            ),
            generation_id=1,
            opus_packets=(b"frame-a", b"frame-b", b"frame-c"),
        )
        with (
            patch("sesame_voice_gateway.app.asyncio.sleep", new=clock.sleep),
            patch("sesame_voice_gateway.app.time.perf_counter", new=clock.perf_counter),
        ):
            await _send_turn_result(websocket, session, result)

        self.assertEqual(clock.sleep_calls, [])
        self.assertEqual(len(websocket.binary_frames), 3)
        controls = [json.loads(frame)["type"] for frame in websocket.text_frames]
        self.assertEqual(controls, ["response.plan", "tts.start", "tts.stop"])

    async def test_downlink_bootstrap_does_not_accumulate_sender_cost(self) -> None:
        clock = _Clock()
        websocket = ClockedWebSocket(clock, send_cost_seconds=0.003)
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
            turn_id="turn_001",
        )
        result = TurnResult(
            transcript=AsrResult(text="测试"),
            agent=AgentResult(text="回复"),
            generation_id=1,
            opus_packets=(b"frame-a", b"frame-b", b"frame-c"),
        )

        with (
            patch("sesame_voice_gateway.app.asyncio.sleep", new=clock.sleep),
            patch("sesame_voice_gateway.app.time.perf_counter", new=clock.perf_counter),
        ):
            await _send_turn_result(websocket, session, result)

        self.assertEqual(len(websocket.packet_sent_at), 3)
        self.assertAlmostEqual(websocket.packet_sent_at[0], 0.0)
        self.assertAlmostEqual(websocket.packet_sent_at[1], 0.003)
        self.assertAlmostEqual(websocket.packet_sent_at[2], 0.006)


if __name__ == "__main__":
    unittest.main()
