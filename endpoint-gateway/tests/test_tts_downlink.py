import json
import sys
import unittest
from pathlib import Path

from fastapi.testclient import TestClient

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sesame_endpoint_gateway.app import create_app
from sesame_endpoint_gateway.protocol import DOWNLINK, unpack_audio_frame


class FakeTts:
    async def synthesize(self, text: str, *, speed: float) -> bytes:
        self.text = text
        self.speed = speed
        return b"fake-pcm"


class FakeOpusEncoder:
    def encode_pcm(self, pcm: bytes) -> tuple[bytes, ...]:
        self.pcm = pcm
        return (b"opus-one", b"opus-two")


class OpenClawTtsDownlinkTests(unittest.TestCase):
    def test_feedback_synthesizes_and_sends_ssm1_tts_turn(self) -> None:
        tts = FakeTts()
        encoder = FakeOpusEncoder()
        client = TestClient(
            create_app(
                device_tokens={"sesame-v3-001": "test-device-token"},
                scene_control_token="test-scene-token",
                tts_synthesizer=tts,
                opus_encoder_factory=lambda: encoder,
            )
        )
        with client.websocket_connect(
            "/v1/device-stream", headers={"Authorization": "Bearer test-device-token"}
        ) as device:
            device.send_json(_hello())
            device.receive_json()
            response = client.post(
                "/v1/openclaw/feedback",
                headers={"Authorization": "Bearer test-scene-token"},
                json=_feedback(),
            )
            expression = device.receive_json()
            start = device.receive_json()
            first = unpack_audio_frame(device.receive_bytes(), expected_direction=DOWNLINK)
            second = unpack_audio_frame(device.receive_bytes(), expected_direction=DOWNLINK)
            stop = device.receive_json()

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["forwarded"], ["expression.set", "tts.start", "audio", "tts.stop"])
        self.assertEqual(tts.text, "你好，很高兴见到你。")
        self.assertEqual(tts.speed, 1.0)
        self.assertEqual(encoder.pcm, b"fake-pcm")
        self.assertEqual(expression["type"], "expression.set")
        self.assertEqual(start["type"], "tts.start")
        self.assertEqual(stop["type"], "tts.stop")
        self.assertEqual(first.sequence, 0)
        self.assertEqual(second.sequence, 1)
        self.assertEqual(first.generation_id, start["payload"]["generation_id"])
        self.assertEqual(second.generation_id, first.generation_id)
        self.assertEqual(first.payload, b"opus-one")
        self.assertEqual(second.payload, b"opus-two")


def _hello() -> dict:
    return {
        "v": 1,
        "type": "session.hello",
        "session_id": None,
        "turn_id": None,
        "request_id": None,
        "sequence": 0,
        "timestamp_ms": 1,
        "payload": {
            "device_id": "sesame-v3-001",
            "gateway_id": "sesame-edge",
            "conversation_id": None,
            "protocol_version": 1,
            "audio": {"codec": "opus", "sample_rate": 16000, "channels": 1, "frame_duration_ms": 20},
        },
    }


def _feedback() -> dict:
    return {
        "device_id": "sesame-v3-001",
        "agent_id": "sesame",
        "response": {
            "v": 1,
            "request_id": "asr-001",
            "turn_id": "turn-001",
            "status": "completed",
            "reply": {"text": "你好，很高兴见到你。"},
            "voice": {"voice_id": "sesame_default", "style": "happy", "speed": 1.0},
            "expression": {"name": "happy", "ttl_ms": 3000},
            "actions": [],
        },
    }


if __name__ == "__main__":
    unittest.main()
