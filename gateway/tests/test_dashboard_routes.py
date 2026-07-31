from __future__ import annotations

import unittest
from unittest.mock import patch

from fastapi.testclient import TestClient

from sesame_voice_gateway.app import create_app
from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.observability import ObservabilityStore


class DashboardRoutesTest(unittest.TestCase):
    def setUp(self) -> None:
        settings = Settings(
            _env_file=None,
            device_tokens={"device": "token"},
            device_users={"device": "user"},
            allow_remote_speech=True,
            dashscope_api_key="test-key",
            openclaw_token="test-token",
            openclaw_session_key_secret="test-secret",
        )
        self.store = ObservabilityStore(max_events=20, expose_debug_content=False)
        self.app = create_app(settings, pipeline=object(), observability=self.store)  # type: ignore[arg-type]

    def test_dashboard_and_snapshot_are_available_to_a_local_browser(self) -> None:
        with TestClient(self.app, client=("127.0.0.1", 4321)) as client:
            dashboard = client.get("/dashboard")
            snapshot = client.get("/api/observability/snapshot")

        self.assertEqual(dashboard.status_code, 200)
        self.assertIn("VOICE", dashboard.text)
        self.assertIn("最新下发计划", dashboard.text)
        self.assertIn("actions=[]", dashboard.text)
        self.assertEqual(snapshot.status_code, 200)
        self.assertFalse(snapshot.json()["debug_content"])

    def test_dashboard_rejects_a_lan_browser(self) -> None:
        with TestClient(self.app, client=("192.168.88.23", 4321)) as client:
            response = client.get("/dashboard")

        self.assertEqual(response.status_code, 403)

    def test_dashboard_accepts_the_computers_lan_address(self) -> None:
        with patch(
            "sesame_voice_gateway.app._local_interface_addresses",
            return_value={"127.0.0.1", "::1", "192.168.88.21"},
        ):
            with TestClient(self.app, client=("192.168.88.21", 4321)) as client:
                response = client.get("/dashboard")

        self.assertEqual(response.status_code, 200)


if __name__ == "__main__":
    unittest.main()
