from __future__ import annotations

import json
import unittest

from sesame_voice_gateway.openclaw.client import OpenClawProtocolError, parse_agent_response
from sesame_voice_gateway.providers.base import AgentToolCall


class OpenClawToolCallTest(unittest.TestCase):
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
