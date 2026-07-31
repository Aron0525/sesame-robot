from __future__ import annotations

import unittest

from sesame_voice_gateway.openclaw.client import (
    build_agent_prompt,
    build_agent_request,
    parse_agent_result,
)
from sesame_voice_gateway.policy import ALLOWED_ACTIONS, ALLOWED_EXPRESSIONS


class DeviceCapabilityContractTest(unittest.TestCase):
    def test_openclaw_receives_the_complete_safe_expression_set(self) -> None:
        request = build_agent_request(
            request_id="req_001",
            conversation_id="conv_001",
            turn_id="turn_001",
            text="测试",
        )

        expected_expressions = [
            "idle",
            "happy",
            "sad",
            "angry",
            "surprised",
            "sleepy",
            "love",
            "excited",
            "confused",
            "thinking",
        ]
        self.assertEqual(request["capabilities"]["expressions"], expected_expressions)
        self.assertEqual(ALLOWED_EXPRESSIONS, frozenset(expected_expressions))

    def test_agent_prompt_requires_a_real_expression_and_allows_no_action(self) -> None:
        request = build_agent_request(
            request_id="req_001",
            conversation_id="conv_001",
            turn_id="turn_001",
            text="你好",
        )

        prompt = build_agent_prompt(request)

        self.assertIn("每一次完成回复都必须选择一个非空的 expression", prompt)
        self.assertIn('actions: []', prompt)
        self.assertNotIn('"default"', request["capabilities"]["expressions"])

    def test_legacy_default_expression_is_normalized_to_idle(self) -> None:
        result = parse_agent_result(
            """{
              "v": 1,
              "request_id": "req_001",
              "turn_id": "turn_001",
              "status": "completed",
              "reply": {"text": "你好"},
              "voice": {"voice_id": "sesame_default", "style": "neutral", "speed": 1.0},
              "expression": {"name": "default", "ttl_ms": 1500},
              "actions": []
            }""",
            expected_request_id="req_001",
            expected_turn_id="turn_001",
        )

        self.assertEqual(result.expression.name, "idle")

    def test_model_actions_remain_a_smaller_safe_subset_than_web_actions(self) -> None:
        self.assertEqual(ALLOWED_ACTIONS, frozenset({"stop", "wave", "rest", "stand"}))


if __name__ == "__main__":
    unittest.main()
