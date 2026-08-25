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
from sesame_voice_gateway.protocol.audio import AudioDirection, unpack_audio_frame
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

    async def test_downlink_reserves_thirty_frames_before_pacing(self) -> None:
        websocket = FakeWebSocket()
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
        delays: list[float] = []

        async def record_delay(seconds: float) -> None:
            delays.append(seconds)

        with patch("sesame_voice_gateway.app.asyncio.sleep", new=record_delay):
            await _send_turn_result(websocket, session, result)

        self.assertEqual(delays, [])
        self.assertEqual(len(websocket.binary_frames), 3)
        frames = [unpack_audio_frame(frame) for frame in websocket.binary_frames]
        self.assertEqual([frame.direction for frame in frames], [AudioDirection.DOWNLINK] * 3)
        self.assertEqual([frame.stream_id for frame in frames], [2, 2, 2])
        self.assertEqual([frame.generation_id for frame in frames], [1, 1, 1])
        self.assertEqual([frame.sequence for frame in frames], [0, 1, 2])
        controls = [json.loads(frame)["type"] for frame in websocket.text_frames]
        self.assertEqual(controls, ["response.plan", "tts.start", "tts.stop"])

    async def test_downlink_paces_after_thirty_frame_reserve(self) -> None:
        websocket = FakeWebSocket()
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
            generation_id=7,
            opus_packets=tuple(f"frame-{index}".encode() for index in range(31)),
        )
        delays: list[float] = []

        async def record_delay(seconds: float) -> None:
            delays.append(seconds)

        with patch("sesame_voice_gateway.app.asyncio.sleep", new=record_delay):
            await _send_turn_result(websocket, session, result)

        self.assertEqual(delays, [0.02])
        frames = [unpack_audio_frame(frame) for frame in websocket.binary_frames]
        self.assertEqual([frame.stream_id for frame in frames], [2] * 31)
        self.assertEqual([frame.generation_id for frame in frames], [7] * 31)
        self.assertEqual([frame.sequence for frame in frames], list(range(31)))


if __name__ == "__main__":
    unittest.main()
