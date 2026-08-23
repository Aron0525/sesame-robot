from __future__ import annotations

import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class FormalVoiceGatewayLayoutTest(unittest.TestCase):
    def test_full_voice_gateway_and_shared_contracts_are_project_owned(self) -> None:
        required = (
            PROJECT_ROOT / "gateway" / "pyproject.toml",
            PROJECT_ROOT
            / "gateway"
            / "apps"
            / "voice_gateway"
            / "src"
            / "sesame_voice_gateway"
            / "pipeline.py",
            PROJECT_ROOT / "gateway" / "tests" / "run_lab_tests.sh",
            PROJECT_ROOT / "contracts" / "schemas" / "control-event.v1.schema.json",
        )
        self.assertEqual([path for path in required if not path.is_file()], [])

    def test_formal_gateway_excludes_local_runtime_and_secret_state(self) -> None:
        forbidden = (
            PROJECT_ROOT / "gateway" / ".env",
            PROJECT_ROOT / "gateway" / ".srl",
            PROJECT_ROOT / "gateway" / "recordings",
        )
        self.assertEqual([path for path in forbidden if path.exists()], [])
        ignore_rules = (PROJECT_ROOT / "gateway" / ".gitignore").read_text(
            encoding="utf-8"
        )
        self.assertIn(".venv", ignore_rules)
        self.assertIn(".env", ignore_rules)


if __name__ == "__main__":
    unittest.main()
