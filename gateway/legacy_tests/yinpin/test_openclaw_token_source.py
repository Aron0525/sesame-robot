from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from sesame_voice_gateway.app import _build_pipeline
from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.openclaw.client import OpenClawAgentProvider


class OpenClawTokenSourceTest(unittest.TestCase):
    def test_direct_provider_uses_current_local_openclaw_token(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "openclaw.json"
            config_path.write_text(
                json.dumps({"gateway": {"auth": {"token": "current-token"}}}),
                encoding="utf-8",
            )
            settings = Settings(
                allow_remote_speech=True,
                dashscope_api_key="dashscope-test-key",
                provider_mode="openclaw",
                openclaw_token="stale-copied-token",
                openclaw_session_key_secret="session-secret",
                openclaw_gateway_config_file=config_path,
            )

            pipeline = _build_pipeline(settings)

        self.assertIsInstance(pipeline._agent, OpenClawAgentProvider)
        self.assertEqual(pipeline._agent.token, "current-token")


if __name__ == "__main__":
    unittest.main()
