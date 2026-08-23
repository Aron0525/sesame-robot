import unittest

from sesame_endpoint_gateway.app import create_app
from sesame_endpoint_gateway.scenes import DEFAULT_SCENE_ID, SCENES, active_profile, scene_catalog
from fastapi.testclient import TestClient


class SceneProfileTests(unittest.TestCase):
    def test_normal_mode_is_separate_from_the_three_scenes(self) -> None:
        self.assertEqual(DEFAULT_SCENE_ID, "normal")
        self.assertEqual(list(SCENES), ["learning", "children", "work"])
        normal = active_profile(DEFAULT_SCENE_ID)
        self.assertEqual(normal["kind"], "normal")
        self.assertEqual(normal["agent_id"], "sesame")
        self.assertEqual(normal["knowledge_base"], "kb-normal")

    def test_every_scene_maps_to_its_own_agent_and_future_knowledge_base(self) -> None:
        expected = {
            "learning": ("sesame-learning", "kb-learning"),
            "children": ("sesame-children", "kb-children"),
            "work": ("sesame-work", "kb-work"),
        }
        catalog = {profile["id"]: profile for profile in scene_catalog()}
        self.assertEqual(set(catalog), set(expected))
        for scene_id, (agent_id, knowledge_base) in expected.items():
            with self.subTest(scene_id=scene_id):
                self.assertEqual(catalog[scene_id]["kind"], "scene")
                self.assertEqual(catalog[scene_id]["agent_id"], agent_id)
                self.assertEqual(catalog[scene_id]["knowledge_base"], knowledge_base)

    def test_console_starts_in_normal_mode_and_can_select_learning(self) -> None:
        client = TestClient(create_app(device_tokens={"sesame-v3-001": "test-device-token"}))
        hub = client.app.state.hub
        self.assertEqual(hub.scene_for("sesame-v3-001")["id"], "normal")
        changed = client.app.state.hub
        self.assertIsNotNone(changed)


if __name__ == "__main__":
    unittest.main()

class ModeSelectionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.client = TestClient(
            create_app(
                device_tokens={"sesame-v3-001": "test-device-token"},
                scene_control_token="test-scene-token",
            )
        )

    def test_console_can_return_from_a_scene_to_independent_normal_mode(self) -> None:
        from tests.test_device_stream import _hello

        with self.client.websocket_connect(
            "/v1/device-stream", headers={"Authorization": "Bearer test-device-token"}
        ) as device:
            device.send_json(_hello())
            device.receive_json()
            with self.client.websocket_connect("/v1/console") as console:
                console.receive_json()
                response = self.client.post(
                    "/v1/openclaw/scene",
                    headers={"Authorization": "Bearer test-scene-token"},
                    json={"device_id": "sesame-v3-001", "scene_id": "work"},
                )
                self.assertEqual(response.status_code, 200)
                console.receive_json()

                console.send_json(
                    {"type": "mode.select", "request_id": "return-normal", "payload": {"mode_id": "normal"}}
                )
                changed = console.receive_json()
                accepted = console.receive_json()

        self.assertEqual(changed["type"], "scene.changed")
        self.assertEqual(changed["payload"]["scene"]["id"], "normal")
        self.assertEqual(accepted["type"], "command.accepted")
        self.assertEqual(accepted["payload"]["command"], "mode.select")

    def test_console_rejects_unknown_mode(self) -> None:
        event_type, payload, error = __import__("sesame_endpoint_gateway.app", fromlist=["validate_console_command"]).validate_console_command(
            {"type": "mode.select", "request_id": "invalid-mode", "payload": {"mode_id": "unknown"}}
        )
        self.assertIsNone(event_type)
        self.assertIsNone(payload)
        self.assertEqual(error["payload"]["code"], "unknown_mode")
