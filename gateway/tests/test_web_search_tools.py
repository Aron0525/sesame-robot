from __future__ import annotations

import unittest
from unittest.mock import patch

from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.tools.web_search import (
    DashScopeWebSearchProvider,
    WebSearchPolicyError,
    WebSearchRequest,
    validate_web_search_request,
)


class WebSearchToolTest(unittest.IsolatedAsyncioTestCase):
    def test_settings_enable_qwen_plus_web_search_explicitly(self) -> None:
        settings = Settings(
            _env_file=None,
            allow_remote_speech=True,
            dashscope_api_key="test-key",
            openclaw_token="test-token",
            openclaw_session_key_secret="test-secret",
            web_search_enabled=True,
            web_search_model="qwen-plus",
        )

        self.assertTrue(settings.web_search_enabled)
        self.assertEqual(settings.web_search_model, "qwen-plus")

    def test_accepts_a_bounded_search_query(self) -> None:
        request = validate_web_search_request(
            {"query": "深圳明天天气", "freshness_days": 7}
        )

        self.assertEqual(request, WebSearchRequest(query="深圳明天天气", freshness_days=7))

    def test_rejects_unknown_fields_and_an_unbounded_query(self) -> None:
        with self.assertRaises(WebSearchPolicyError):
            validate_web_search_request({"query": "天气", "url": "https://example.test"})
        with self.assertRaises(WebSearchPolicyError):
            validate_web_search_request({"query": "x" * 257})
        with self.assertRaises(WebSearchPolicyError):
            validate_web_search_request({"query": "天气", "freshness_days": 1})

    async def test_uses_qwen_plus_and_returns_a_bounded_source_list(self) -> None:
        provider = DashScopeWebSearchProvider(
            api_key="test-key",
            http_base_url="https://example.test/api/v1",
            model="qwen-plus",
            timeout_seconds=1,
        )
        response = {
            "output": {
                "choices": [{"message": {"content": "深圳明天多云，气温 25°C。"}}],
                "search_info": {
                    "search_results": [
                        {
                            "title": "深圳天气",
                            "url": "https://weather.example.test/shenzhen",
                            "snippet": "多云",
                            "publish_time": "2026-08-03",
                        }
                    ]
                },
            }
        }

        with patch(
            "sesame_voice_gateway.tools.web_search.dashscope.Generation.call",
            return_value=response,
        ) as search_call:
            result = await provider.search(WebSearchRequest(query="深圳明天天气", freshness_days=7))

        self.assertEqual(result.summary, "深圳明天多云，气温 25°C。")
        self.assertEqual(result.sources[0].url, "https://weather.example.test/shenzhen")
        self.assertEqual(search_call.call_args.kwargs["model"], "qwen-plus")
        self.assertTrue(search_call.call_args.kwargs["enable_search"])
        self.assertTrue(search_call.call_args.kwargs["search_options"]["forced_search"])


if __name__ == "__main__":
    unittest.main()
