from __future__ import annotations

import json
import unittest

from sesame_voice_gateway.app import DeviceSession, _send_turn_result
from sesame_voice_gateway.pipeline import TurnResult
from sesame_voice_gateway.providers.base import (
    ActionSpec,
    AgentResult,
    AsrResult,
    ExpressionSpec,
    VoiceSpec,
)
from sesame_voice_gateway.protocol.control import parse_control_event


class FakeWebSocket:
    def __init__(self) -> None:
        self.text_frames: list[str] = []
        self.binary_frames: list[bytes] = []

    async def send_text(self, value: str) -> None:
        self.text_frames.append(value)

    async def send_bytes(self, value: bytes) -> None:
        self.binary_frames.append(value)


class ResponsePlanProtocolTest(unittest.IsolatedAsyncioTestCase):
    def test_response_plan_requires_one_safe_action_and_expression(self) -> None:
        event = parse_control_event(
            json.dumps(
                {
                    "v": 1,
                    "type": "response.plan",
                    "session_id": "ses_001",
                    "turn_id": "turn_001",
                    "request_id": None,
                    "sequence": 4,
                    "timestamp_ms": 1000,
                    "payload": {
                        "generation_id": 7,
                        "expression_id": "happy",
                        "expression_ttl_ms": 1200,
                        "action_id": "wave",
                        "action_request_id": "act_001",
                        "action_duration_ms": 1200,
                        "action_deadline_ms": 6000,
                    },
                }
            )
        )

        self.assertEqual(event.type, "response.plan")

    def test_response_plan_rejects_partial_action(self) -> None:
        with self.assertRaisesRegex(ValueError, "null"):
            parse_control_event(
                json.dumps(
                    {
                        "v": 1,
                        "type": "response.plan",
                        "session_id": "ses_001",
                        "turn_id": "turn_001",
                        "request_id": None,
                        "sequence": 4,
                        "timestamp_ms": 1000,
                        "payload": {
                            "generation_id": 7,
                            "expression_id": "happy",
                            "expression_ttl_ms": 1200,
                            "action_id": None,
                            "action_request_id": "act_001",
                            "action_duration_ms": 1200,
                            "action_deadline_ms": 6000,
                        },
                    }
                )
            )

    async def test_gateway_sends_one_plan_before_tts_opus(self) -> None:
        websocket = FakeWebSocket()
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
            turn_id="turn_001",
        )
        result = TurnResult(
            transcript=AsrResult(text="请挥手"),
            agent=AgentResult(
                text="你好。",
                voice=VoiceSpec(),
                expression=ExpressionSpec(name="happy", ttl_ms=1200),
                actions=(ActionSpec(name="wave", duration_ms=1200),),
            ),
            generation_id=7,
            opus_packets=(b"opus-a", b"opus-b"),
        )

        await _send_turn_result(websocket, session, result)  # type: ignore[arg-type]

        controls = [parse_control_event(frame) for frame in websocket.text_frames]
        self.assertEqual([event.type for event in controls], ["response.plan", "tts.start", "tts.stop"])
        self.assertEqual(controls[0].payload["generation_id"], 7)
        self.assertEqual(controls[0].payload["expression_id"], "happy")
        self.assertEqual(controls[0].payload["action_id"], "wave")
        self.assertEqual(len(websocket.binary_frames), 2)


if __name__ == "__main__":
    unittest.main()
