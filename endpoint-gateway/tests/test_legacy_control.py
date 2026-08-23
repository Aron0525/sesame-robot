import sys
import unittest
from pathlib import Path

from fastapi.testclient import TestClient

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sesame_endpoint_gateway.app import create_app


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
            "audio": {
                "codec": "opus",
                "sample_rate": 16000,
                "channels": 1,
                "frame_duration_ms": 20,
            },
        },
    }


class LegacyControlContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.client = TestClient(
            create_app(device_tokens={"sesame-v3-001": "test-device-token"})
        )

    def test_every_original_console_control_is_forwarded_as_operator_control(self) -> None:
        commands = (
            {"kind": "action", "action": "dance"},
            {"kind": "expression", "expression": "talk_happy"},
            {"kind": "servo", "servo": 1, "angle": 90},
            {
                "kind": "settings",
                "frame_delay_ms": 100,
                "walk_cycles": 10,
                "motor_current_delay_ms": 20,
            },
            {"kind": "wakeword_settings", "wake_threshold_hundredths": 86},
            {"kind": "stop"},
        )
        with self.client.websocket_connect(
            "/v1/device-stream", headers={"Authorization": "Bearer test-device-token"}
        ) as device:
            device.send_json(_hello())
            device.receive_json()
            for command in commands:
                response = self.client.post("/api/local-control/sesame-v3-001", json=command)
                self.assertEqual(response.status_code, 200)
                forwarded = device.receive_json()
                self.assertEqual(forwarded["type"], "operator.control")
                self.assertEqual(forwarded["turn_id"], None)
                self.assertEqual(forwarded["payload"], command)

    def test_console_rejects_out_of_range_manual_servo(self) -> None:
        response = self.client.post(
            "/api/local-control/sesame-v3-001",
            json={"kind": "servo", "servo": 9, "angle": 90},
        )
        self.assertEqual(response.status_code, 422)
        self.assertEqual(response.json()["detail"], "invalid_servo")

    def test_legacy_snapshot_exposes_device_control_result(self) -> None:
        with self.client.websocket_connect(
            "/v1/device-stream", headers={"Authorization": "Bearer test-device-token"}
        ) as device:
            device.send_json(_hello())
            device.receive_json()
            with self.client.websocket_connect("/v1/console") as console:
                console.receive_json()
                response = self.client.post(
                    "/api/local-control/sesame-v3-001",
                    json={"kind": "settings", "frame_delay_ms": 100, "walk_cycles": 10,
                          "motor_current_delay_ms": 20},
                )
                forwarded = device.receive_json()
                request_id = response.json()["request_id"]
                device.send_json(
                    {
                        "v": 1,
                        "type": "action.result",
                        "session_id": "ses-test",
                        "turn_id": None,
                        "request_id": request_id,
                        "sequence": 1,
                        "timestamp_ms": 2,
                        "payload": {"status": "completed", "error_code": None},
                    }
                )
                result = console.receive_json()
                snapshot = self.client.get("/api/observability/snapshot")

        self.assertEqual(forwarded["type"], "operator.control")
        self.assertEqual(result["type"], "action.result")
        event = snapshot.json()["events"][-1]
        self.assertEqual(event["stage"], "device.action")
        self.assertEqual(event["status"], "completed")
        self.assertEqual(event["details"]["request_id"], request_id)


if __name__ == "__main__":
    unittest.main()
