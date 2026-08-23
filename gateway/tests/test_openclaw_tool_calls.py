from __future__ import annotations

import json
import unittest

from sesame_voice_gateway.openclaw.client import (
    OpenClawProtocolError,
    build_agent_prompt_with_tools,
    parse_agent_response,
)
from sesame_voice_gateway.providers.base import AgentToolCall


class OpenClawToolCallTest(unittest.TestCase):
    def test_web_search_prompt_lists_the_only_allowed_freshness_values(self) -> None:
        prompt = build_agent_prompt_with_tools({"request_id": "req_001"}, allow_web_search=True)

        self.assertIn("freshness_days 可省略", prompt)
        self.assertIn("7、30、180、365", prompt)
        self.assertIn("不得使用 1", prompt)
        self.assertIn("天气、新闻、实时路况、汇率、价格、赛程", prompt)
        self.assertIn("通用知识、闲聊、设备控制不要使用 web_search", prompt)

    def test_accepts_only_web_search_when_the_gateway_enabled_it(self) -> None:
        response = parse_agent_response(
            json.dumps(
                {
                    "v": 2,
                    "request_id": "req_001",
                    "turn_id": "turn_001",
                    "status": "requires_tool",
                    "tool_call": {"name": "web_search", "arguments": {"query": "深圳天气", "freshness_days": 7}},
                }
            ),
            expected_request_id="req_001",
            expected_turn_id="turn_001",
            allow_web_search=True,
        )

        self.assertEqual(
            response,
            AgentToolCall("web_search", {"query": "深圳天气", "freshness_days": 7}),
        )

    def test_rejects_tool_calls_after_the_gateway_has_returned_search_evidence(self) -> None:
        with self.assertRaises(OpenClawProtocolError):
            parse_agent_response(
                '{"v":2,"request_id":"req_001","turn_id":"turn_001","status":"requires_tool","tool_call":{"name":"web_search","arguments":{"query":"深圳天气"}}}',
                expected_request_id="req_001",
                expected_turn_id="turn_001",
                allow_web_search=False,
            )


if __name__ == "__main__":
    unittest.main()
