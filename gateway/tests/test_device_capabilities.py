from __future__ import annotations

import json
import unittest

from sesame_voice_gateway.openclaw.client import (
    ALLOWED_ACTIONS as OPENCLAW_ALLOWED_ACTIONS,
    build_agent_prompt,
    build_agent_request,
    parse_agent_result,
)
from sesame_voice_gateway.policy import (
    ALLOWED_ACTIONS as POLICY_ALLOWED_ACTIONS,
    ALLOWED_EXPRESSIONS,
    validate_agent_result,
)


class DeviceCapabilityContractTest(unittest.TestCase):
    def test_openclaw_receives_no_actions_and_the_expression_catalog(self) -> None:
        request = build_agent_request(
            request_id="req_001",
            conversation_id="conv_001",
            turn_id="turn_001",
            text="测试",
        )

        expected_actions: list[str] = []
        expected_expressions = [
            "walk",
            "rest",
            "swim",
            "dance",
            "wave",
            "point",
            "cute",
            "pushup",
            "freaky",
            "bow",
            "worm",
            "shake",
            "shrug",
            "dead",
            "crab",
            "idle",
            "idle_blink",
            "happy",
            "talk_happy",
            "sad",
            "talk_sad",
            "angry",
            "talk_angry",
            "surprised",
            "talk_surprised",
            "sleepy",
            "talk_sleepy",
            "love",
            "talk_love",
            "excited",
            "talk_excited",
            "confused",
            "talk_confused",
            "thinking",
            "talk_thinking",
        ]
        self.assertEqual(request["capabilities"]["actions"], expected_actions)
        self.assertEqual(request["capabilities"]["expressions"], expected_expressions)
        self.assertEqual(OPENCLAW_ALLOWED_ACTIONS, expected_actions)
        self.assertTrue(POLICY_ALLOWED_ACTIONS)
        self.assertEqual(ALLOWED_EXPRESSIONS, frozenset(expected_expressions))

    def test_agent_prompt_requires_a_real_expression_and_forbids_actions(self) -> None:
        request = build_agent_request(
            request_id="req_001",
            conversation_id="conv_001",
            turn_id="turn_001",
            text="你好",
        )

        prompt = build_agent_prompt(request)

        self.assertIn("每一次完成回复都必须选择一个非空的 expression", prompt)
        self.assertIn("actions 必须始终输出 []", prompt)
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

    def test_openclaw_metadata_does_not_break_the_device_response_contract(self) -> None:
        result = parse_agent_result(
            """{
              "v": 1,
              "request_id": "req_001",
              "turn_id": "turn_001",
              "type": "agent_response",
              "conversation_id": "agent:sesame:conversation:opaque",
              "memory_updates": [],
              "status": "completed",
              "reply": {"text": "状态正常"},
              "voice": {"voice_id": "sesame_default", "style": "neutral", "speed": 1.0},
              "expression": {"name": "idle", "ttl_ms": 1500},
              "actions": []
            }""",
            expected_request_id="req_001",
            expected_turn_id="turn_001",
        )

        self.assertEqual(result.text, "状态正常")

    def test_model_action_capability_is_empty(self) -> None:
        self.assertEqual(OPENCLAW_ALLOWED_ACTIONS, [])

    def test_agent_actions_are_dropped_before_reaching_the_plan(self) -> None:
        request = build_agent_request(
            request_id="req_001",
            conversation_id="conv_001",
            turn_id="turn_001",
            text="测试",
        )

        def parse(payload: dict[str, object]) -> None:
            result = parse_agent_result(
                json.dumps(payload),
                expected_request_id="req_001",
                expected_turn_id="turn_001",
            )
            self.assertIs(validate_agent_result(result), result)

        result = parse_agent_result(
            json.dumps(
                {
                    "v": 1,
                    "request_id": "req_001",
                    "turn_id": "turn_001",
                    "status": "completed",
                    "reply": {"text": "收到"},
                    "voice": {
                        "voice_id": "sesame_default",
                        "style": "neutral",
                        "speed": 1.0,
                    },
                    "expression": {"name": "idle", "ttl_ms": 1500},
                    "actions": [{"name": "wave", "duration_ms": 1000}],
                }
            ),
            expected_request_id="req_001",
            expected_turn_id="turn_001",
        )
        self.assertEqual(result.actions, ())

        for expression in request["capabilities"]["expressions"]:
            parse(
                {
                    "v": 1,
                    "request_id": "req_001",
                    "turn_id": "turn_001",
                    "status": "completed",
                    "reply": {"text": "收到"},
                    "voice": {
                        "voice_id": "sesame_default",
                        "style": "neutral",
                        "speed": 1.0,
                    },
                    "expression": {"name": expression, "ttl_ms": 1500},
                    "actions": [],
                }
            )


if __name__ == "__main__":
    unittest.main()
