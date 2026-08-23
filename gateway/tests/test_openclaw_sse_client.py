from __future__ import annotations

import unittest

from sesame_voice_gateway.openclaw.sse_client import OpenClawSseAgentProvider


class _Response:
    headers = {"content-type": "text/event-stream; charset=utf-8"}

    def __init__(self) -> None:
        self.closed = False

    def raise_for_status(self) -> None:
        return None

    def iter_lines(self, **_: object):
        yield "event: reply.delta"
        yield 'data: {"turn_id":"turn_001","seq":1,"text":"你好"}'
        yield ""
        yield "event: reply.sentence"
        yield 'data: {"turn_id":"turn_001","seq":2,"text":"，世界。"}'
        yield ""
        yield "event: reply.final"
        yield 'data: {"turn_id":"turn_001","seq":3,"expression":"happy","actions":[]}'
        yield ""

    def close(self) -> None:
        self.closed = True


class OpenClawSseAgentProviderTest(unittest.IsolatedAsyncioTestCase):
    async def test_posts_only_the_sanitized_agent_request_and_yields_deltas_and_sentences(self) -> None:
        response = _Response()
        captured: dict[str, object] = {}

        def post(**kwargs: object) -> _Response:
            captured.update(kwargs)
            return response

        provider = OpenClawSseAgentProvider(
            url="http://127.0.0.1:18790/v1/sesame/reply-stream",
            token="test-token",
            agent_id="sesame",
            session_key_secret="test-session-secret",
            post=post,
        )

        events = [
            event
            async for event in provider.stream_reply(
                text="和我打个招呼",
                device_id="device_001",
                user_id="user_001",
                conversation_id="conv_001",
                turn_id="turn_001",
            )
        ]

        self.assertEqual(
            [event.text for event in events if hasattr(event, "text")],
            ["你好", "，世界。", "你好，世界。"],
        )
        self.assertTrue(response.closed)
        self.assertEqual(captured["headers"], {"Authorization": "Bearer test-token", "Accept": "text/event-stream"})
        request = captured["json"]
        self.assertEqual(set(request), {"agent_id", "session_key", "request"})
        self.assertNotIn("device_001", str(request))
        self.assertNotIn("user_001", str(request))


if __name__ == "__main__":
    unittest.main()
