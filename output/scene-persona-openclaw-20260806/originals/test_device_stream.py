import json
import sys
import unittest
from pathlib import Path

from fastapi.testclient import TestClient

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sesame_endpoint_gateway.app import create_app
from sesame_endpoint_gateway.protocol import AudioFrameError, unpack_audio_frame


class DeviceStreamTests(unittest.TestCase):
    def setUp(self) -> None:
        self.client = TestClient(
            create_app(device_tokens={"sesame-v3-001": "test-device-token"})
        )

    def test_authenticated_hello_returns_negotiated_audio_contract(self) -> None:
        with self.client.websocket_connect(
            "/v1/device-stream",
            headers={"Authorization": "Bearer test-device-token"},
        ) as websocket:
            websocket.send_text(
                json.dumps(
                    {
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
                            "audio": {
                                "codec": "opus",
                                "sample_rate": 16000,
                                "channels": 1,
                                "frame_duration_ms": 20,
                            },
                        },
                    }
                )
            )

            message = websocket.receive_json()

        self.assertEqual(message["type"], "session.ready")
        self.assertEqual(message["sequence"], 0)
        self.assertEqual(message["payload"]["gateway_id"], "sesame-edge")
        self.assertEqual(message["payload"]["audio"]["codec"], "opus")

    def test_unauthenticated_connection_is_rejected_before_hello(self) -> None:
        with self.assertRaises(Exception):
            with self.client.websocket_connect("/v1/device-stream"):
                pass

    def test_malformed_or_downlink_audio_is_rejected_at_gateway_boundary(self) -> None:
        with self.assertRaises(AudioFrameError):
            unpack_audio_frame(b"not-an-ssm1-frame", expected_direction=0)

        downlink = b"SSM1\x01\x01\x00\x00" + (b"\x00" * 20) + b"\x00\x00\x00\x01x"
        with self.assertRaises(AudioFrameError):
            unpack_audio_frame(downlink, expected_direction=0)

    def test_console_serves_a_single_site_and_forwards_allowed_action(self) -> None:
        page = self.client.get("/")
        self.assertEqual(page.status_code, 200)
        self.assertIn("Sesame Field Console", page.text)

        with self.client.websocket_connect(
            "/v1/device-stream",
            headers={"Authorization": "Bearer test-device-token"},
        ) as device:
            device.send_json(_hello())
            ready = device.receive_json()

            with self.client.websocket_connect("/v1/console") as console:
                snapshot = console.receive_json()
                self.assertEqual(snapshot["type"], "console.snapshot")
                self.assertEqual(snapshot["payload"]["device_id"], "sesame-v3-001")

                console.send_json(
                    {
                        "type": "action.execute",
                        "request_id": "web-wave-1",
                        "payload": {"action": "wave", "duration_ms": 1000},
                    }
                )

                forwarded = device.receive_json()
                accepted = console.receive_json()

        self.assertEqual(forwarded["type"], "action.execute")
        self.assertEqual(forwarded["sequence"], ready["sequence"] + 1)
        self.assertEqual(forwarded["request_id"], "web-wave-1")
        self.assertEqual(forwarded["payload"]["action"], "wave")
        self.assertGreater(forwarded["payload"]["deadline_ms"], forwarded["timestamp_ms"])
        self.assertEqual(accepted["type"], "command.accepted")

    def test_console_rejects_unknown_actions_and_broadcasts_device_status(self) -> None:
        with self.client.websocket_connect(
            "/v1/device-stream",
            headers={"Authorization": "Bearer test-device-token"},
        ) as device:
            device.send_json(_hello())
            device.receive_json()

            with self.client.websocket_connect("/v1/console") as console:
                console.receive_json()
                console.send_json(
                    {
                        "type": "action.execute",
                        "request_id": "web-dance-1",
                        "payload": {"action": "dance", "duration_ms": 1000},
                    }
                )
                rejected = console.receive_json()
                self.assertEqual(rejected["type"], "command.rejected")
                self.assertIn("unknown_action", rejected["payload"]["code"])

                device.send_json(
                    {
                        "v": 1,
                        "type": "status.update",
                        "session_id": "ses-test",
                        "turn_id": None,
                        "request_id": None,
                        "sequence": 1,
                        "timestamp_ms": 2,
                        "payload": {"status": "IDLE", "rssi": -54, "event": {"text": "ready"}},
                    }
                )
                status = console.receive_json()

        self.assertEqual(status["type"], "status.update")
        self.assertEqual(status["payload"]["rssi"], -54)


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


if __name__ == "__main__":
    unittest.main()
