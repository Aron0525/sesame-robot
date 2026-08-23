import unittest

from fastapi.testclient import TestClient

from sesame_endpoint_gateway.app import GatewayHub, create_app


class ModeSelectionRouterTests(unittest.TestCase):
    def test_generic_switch_request_announces_hard_coded_options(self) -> None:
        hub = GatewayHub()
        result = self._run(hub.handle_mode_text("sesame-v3-001", "切换模式", source="asr"))

        self.assertEqual(result["status"], "selection_required")
        self.assertEqual(
            result["announcement"],
            "可以切换到学习模式、儿童模式或工作模式。请说“选择学习”“选择儿童”或“选择工作”；也可以说“切换回正常模式”。",
        )
        self.assertTrue(result["pending"])
        self.assertEqual(hub.scene_for("sesame-v3-001")["id"], "normal")

    def test_choice_after_generic_prompt_switches_to_selected_scene(self) -> None:
        hub = GatewayHub()
        self._run(hub.handle_mode_text("sesame-v3-001", "我要切换模式", source="asr"))
        result = self._run(hub.handle_mode_text("sesame-v3-001", "选择儿童", source="asr"))

        self.assertEqual(result["status"], "switched")
        self.assertFalse(result["pending"])
        self.assertEqual(result["scene"]["id"], "children")
        self.assertEqual(result["scene"]["agent_id"], "sesame-children")
        self.assertEqual(result["announcement"], "已切换到儿童模式。")

    def test_unrecognized_choice_keeps_selection_pending(self) -> None:
        hub = GatewayHub()
        self._run(hub.handle_mode_text("sesame-v3-001", "切换模式", source="asr"))
        result = self._run(hub.handle_mode_text("sesame-v3-001", "随便", source="asr"))

        self.assertEqual(result["status"], "selection_required")
        self.assertTrue(result["pending"])
        self.assertIn("选择学习", result["announcement"])

    @staticmethod
    def _run(awaitable):
        import asyncio

        return asyncio.run(awaitable)


class ModeSelectionEndpointTests(unittest.TestCase):
    def setUp(self) -> None:
        self.client = TestClient(
            create_app(
                device_tokens={"sesame-v3-001": "test-device-token"},
                scene_control_token="test-scene-token",
            )
        )

    def test_authorized_mode_text_endpoint_prompts_then_selects(self) -> None:
        headers = {"Authorization": "Bearer test-scene-token"}
        prompt = self.client.post(
            "/v1/openclaw/mode-command",
            headers=headers,
            json={"device_id": "sesame-v3-001", "text": "切换模式"},
        )
        selected = self.client.post(
            "/v1/openclaw/mode-command",
            headers=headers,
            json={"device_id": "sesame-v3-001", "text": "选择工作"},
        )

        self.assertEqual(prompt.status_code, 200)
        self.assertEqual(prompt.json()["status"], "selection_required")
        self.assertEqual(selected.status_code, 200)
        self.assertEqual(selected.json()["scene"]["id"], "work")


if __name__ == "__main__":
    unittest.main()
