from __future__ import annotations

import json
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class OpenClawSearchBoundaryTest(unittest.TestCase):
    def test_sesame_agents_cannot_call_native_web_search(self) -> None:
        config = json.loads(
            (PROJECT_ROOT / "ops/openclaw/config/agents.template.json").read_text(
                encoding="utf-8"
            )
        )
        for agent in config["agents"]["list"]:
            with self.subTest(agent=agent["id"]):
                self.assertNotIn("web_search", agent["tools"]["allow"])
                self.assertIn("web_search", agent["tools"]["deny"])

    def test_workspace_requests_search_through_gateway_contract_only(self) -> None:
        workspace_root = PROJECT_ROOT / "ops/openclaw/workspaces"
        documents = [workspace_root / "shared/TOOLS.md"] + [
            workspace_root / name / "AGENTS.md"
            for name in ("sesame", "sesame-learning", "sesame-children", "sesame-work")
        ]
        for document in documents:
            with self.subTest(document=document.name):
                content = document.read_text(encoding="utf-8")
                self.assertIn("requires_tool", content)
                self.assertIn("Gateway", content)
                self.assertIn("不能直接调用", content)


if __name__ == "__main__":
    unittest.main()
