from __future__ import annotations

import unittest

from fastapi.testclient import TestClient

from sesame_voice_gateway.app import create_app
from sesame_voice_gateway.config import Settings


class StreamingLabIdentityTest(unittest.TestCase):
    def test_rejects_the_stable_gateway_port(self) -> None:
        with self.assertRaisesRegex(ValueError, "SESAME_PORT"):
            Settings(
                _env_file=None,
                port=8765,
                device_tokens={"sesame-stream-lab-001": "test-device-token"},
                device_users={"sesame-stream-lab-001": "usr_stream_lab"},
                allow_remote_speech=True,
                dashscope_api_key="test-key",
                openclaw_token="test-openclaw-token",
                openclaw_session_key_secret="test-session-secret",
            )

    def test_only_the_v2_lab_endpoint_accepts_the_lab_gateway_identity(self) -> None:
        settings = Settings(
            _env_file=None,
            device_tokens={"sesame-stream-lab-001": "test-device-token"},
            device_users={"sesame-stream-lab-001": "usr_stream_lab"},
            allow_remote_speech=True,
            dashscope_api_key="test-key",
            openclaw_token="test-openclaw-token",
            openclaw_session_key_secret="test-session-secret",
        )
        app = create_app(settings, pipeline=object())  # type: ignore[arg-type]

        with TestClient(app) as client:
            with client.websocket_connect(
                "/v2/device-stream", headers={"Authorization": "Bearer test-device-token"}
            ) as websocket:
                websocket.send_json(
                    {
                        "v": 1,
                        "type": "session.hello",
                        "session_id": None,
                        "turn_id": None,
                        "request_id": None,
                        "sequence": 0,
                        "timestamp_ms": 1,
                        "payload": {
                            "device_id": "sesame-stream-lab-001",
                            "gateway_id": "gw_stream_lab",
                            "conversation_id": None,
                            "protocol_version": 1,
                            "audio": {
                                "codec": "opus",
                                "sample_rate": 16_000,
                                "channels": 1,
                                "frame_duration_ms": 20,
                            },
                        },
                    }
                )
                ready = websocket.receive_json()

        self.assertEqual(ready["payload"]["gateway_id"], "gw_stream_lab")


if __name__ == "__main__":
    unittest.main()
