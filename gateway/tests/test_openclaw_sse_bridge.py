from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from fastapi.testclient import TestClient

from sesame_voice_gateway.openclaw.sse_bridge import (
    OpenClawChatEventMapper,
    OpenClawWebSocketStreamer,
    create_bridge_app,
    load_openclaw_gateway_token,
)


def _chat_frame(*, run_id: str, state: str, **payload: object) -> dict[str, object]:
    return {"type": "event", "event": "chat", "payload": {"runId": run_id, "state": state, **payload}}


class OpenClawChatEventMapperTest(unittest.TestCase):
    def test_maps_contiguous_chat_deltas_to_action_free_sse_events(self) -> None:
        mapper = OpenClawChatEventMapper(turn_id="turn_001", run_id="run_001")

        first = mapper.feed(_chat_frame(run_id="run_001", state="delta", deltaText="今天"))
        second = mapper.feed(_chat_frame(run_id="run_001", state="delta", deltaText="天气不错。"))
        final = mapper.feed(_chat_frame(run_id="run_001", state="final"))

        self.assertEqual(first.event, "reply.delta")
        self.assertEqual(first.data, {"turn_id": "turn_001", "seq": 1, "text": "今天"})
        self.assertEqual(second.event, "reply.delta")
        self.assertEqual(second.data, {"turn_id": "turn_001", "seq": 2, "text": "天气不错。"})
        self.assertEqual(final.event, "reply.final")
        self.assertEqual(
            final.data,
            {"turn_id": "turn_001", "seq": 3, "expression": "idle", "actions": []},
        )

    def test_rejects_a_replacement_that_rewrites_emitted_text(self) -> None:
        mapper = OpenClawChatEventMapper(turn_id="turn_001", run_id="run_001")
        mapper.feed(_chat_frame(run_id="run_001", state="delta", deltaText="今天"))

        with self.assertRaisesRegex(ValueError, "rewrite"):
            mapper.feed(
                _chat_frame(
                    run_id="run_001",
                    state="delta",
                    deltaText="明天",
                    replace=True,
                )
            )


class OpenClawSseBridgeEndpointTest(unittest.TestCase):
    def test_authenticated_request_returns_strict_sse_events(self) -> None:
        async def stream_openclaw(**_: object):
            yield _chat_frame(run_id="run_001", state="delta", deltaText="你好，世界。")
            yield _chat_frame(run_id="run_001", state="final")

        app = create_bridge_app(bridge_token="bridge-token", stream_openclaw=stream_openclaw)
        request = {
            "agent_id": "sesame",
            "session_key": "agent:sesame:conversation:abc",
            "request": {
                "v": 1,
                "request_id": "req_001",
                "conversation_id": "conv_001",
                "turn_id": "turn_001",
                "input": {"type": "text", "text": "打个招呼"},
            },
        }

        with TestClient(app) as client:
            response = client.post(
                "/v1/sesame/reply-stream",
                headers={"Authorization": "Bearer bridge-token"},
                json=request,
            )

        self.assertEqual(response.status_code, 200)
        self.assertTrue(response.headers["content-type"].startswith("text/event-stream"))
        self.assertIn('event: reply.delta\ndata: {"turn_id":"turn_001","seq":1,"text":"你好，世界。"}', response.text)
        self.assertIn('event: reply.final\ndata: {"turn_id":"turn_001","seq":2,"expression":"idle","actions":[]}', response.text)

    def test_rejects_a_request_without_the_lab_bearer_token(self) -> None:
        async def stream_openclaw(**_: object):
            if False:
                yield {}

        app = create_bridge_app(bridge_token="bridge-token", stream_openclaw=stream_openclaw)
        with TestClient(app) as client:
            response = client.post("/v1/sesame/reply-stream", json={})

        self.assertEqual(response.status_code, 401)


class _WebSocket:
    def __init__(self, frames: list[dict[str, object]]) -> None:
        self._frames = iter(json.dumps(frame) for frame in frames)
        self.sent: list[dict[str, object]] = []

    async def __aenter__(self) -> _WebSocket:
        return self

    async def __aexit__(self, *_: object) -> None:
        return None

    async def recv(self) -> str:
        return next(self._frames)

    async def send(self, value: str) -> None:
        self.sent.append(json.loads(value))


class OpenClawWebSocketStreamerTest(unittest.IsolatedAsyncioTestCase):
    async def test_authenticates_then_yields_only_the_requested_chat_run(self) -> None:
        websocket = _WebSocket(
            [
                {"type": "event", "event": "connect.challenge", "payload": {"nonce": "nonce"}},
                {"type": "res", "id": "connect_001", "ok": True, "payload": {"type": "hello-ok", "protocol": 4}},
                {"type": "res", "id": "chat_001", "ok": True, "payload": {"runId": "run_001"}},
                _chat_frame(run_id="other", state="delta", deltaText="忽略"),
                _chat_frame(run_id="run_001", state="delta", deltaText="你好"),
                _chat_frame(run_id="run_001", state="final"),
            ]
        )
        identifiers = iter(["connect_001", "chat_001"])
        streamer = OpenClawWebSocketStreamer(
            url="ws://127.0.0.1:18789",
            token="openclaw-token",
            connect=lambda *_args, **_kwargs: websocket,
            make_id=lambda _prefix: next(identifiers),
        )

        frames = [
            frame
            async for frame in streamer(
                agent_id="sesame",
                session_key="agent:sesame:conversation:abc",
                text="打个招呼",
                request_id="req_001",
                turn_id="turn_001",
            )
        ]

        self.assertEqual([frame["payload"]["runId"] for frame in frames], ["run_001", "run_001"])
        self.assertEqual([frame["payload"]["state"] for frame in frames], ["delta", "final"])
        self.assertEqual([message["method"] for message in websocket.sent], ["connect", "chat.send"])
        self.assertEqual(websocket.sent[0]["params"]["client"]["id"], "gateway-client")
        self.assertIn("只输出自然语言", websocket.sent[1]["params"]["message"])


class OpenClawGatewayTokenTest(unittest.TestCase):
    def test_reads_only_the_gateway_auth_token_from_the_existing_local_config(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "openclaw.json"
            config_path.write_text(
                json.dumps({"gateway": {"auth": {"token": "upstream-token"}}}),
                encoding="utf-8",
            )

            token = load_openclaw_gateway_token(config_path)

        self.assertEqual(token, "upstream-token")


if __name__ == "__main__":
    unittest.main()
