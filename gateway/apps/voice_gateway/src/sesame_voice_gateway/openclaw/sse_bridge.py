"""Translate trusted OpenClaw chat deltas into the lab's strict SSE contract."""

from __future__ import annotations

import asyncio
import hmac
import json
import uuid
from collections.abc import AsyncIterator, Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import StreamingResponse
import websockets

from sesame_voice_gateway.openclaw.client import (
    OPENCLAW_PROTOCOL_VERSION,
    OpenClawProtocolError,
    build_chat_send_request,
    build_connect_request,
    parse_challenge,
)


@dataclass(frozen=True, slots=True)
class BridgeSseEvent:
    event: str
    data: dict[str, Any]


OpenClawFrameStream = Callable[..., AsyncIterator[dict[str, object]]]


@dataclass(frozen=True, slots=True)
class _BridgeRequest:
    agent_id: str
    session_key: str
    request_id: str
    turn_id: str
    text: str


@dataclass(slots=True)
class OpenClawWebSocketStreamer:
    """Open one authenticated OpenClaw connection and expose one chat run."""

    url: str
    token: str
    timeout_seconds: float = 60.0
    connect: Callable[..., Any] = websockets.connect
    make_id: Callable[[str], str] = lambda prefix: f"{prefix}_{uuid.uuid4().hex}"

    def __post_init__(self) -> None:
        parsed = urlparse(self.url)
        if parsed.scheme not in {"ws", "wss"} or parsed.hostname not in {
            "127.0.0.1",
            "::1",
            "localhost",
        }:
            raise ValueError("OpenClaw bridge may connect only to a loopback WebSocket")
        if not self.token:
            raise ValueError("OpenClaw Gateway token is required")
        if self.timeout_seconds <= 0:
            raise ValueError("OpenClaw timeout must be positive")

    async def __call__(
        self,
        *,
        agent_id: str,
        session_key: str,
        text: str,
        request_id: str,
        turn_id: str,
    ) -> AsyncIterator[dict[str, object]]:
        if not session_key.startswith(f"agent:{agent_id}:"):
            raise ValueError("session key does not belong to the requested agent")
        if not request_id or not turn_id or not text.strip():
            raise ValueError("OpenClaw streaming request is invalid")
        async with self.connect(
            self.url,
            max_size=1_048_576,
            ping_interval=15,
            ping_timeout=15,
            close_timeout=2,
        ) as websocket:
            await self._authenticate(websocket)
            chat_request_id = self.make_id("chat")
            await websocket.send(
                json.dumps(
                    build_chat_send_request(
                        request_id=chat_request_id,
                        session_key=session_key,
                        message=_build_streaming_prompt(text),
                        idempotency_key=f"stream:{request_id}",
                    )
                )
            )
            acknowledgement = await self._receive_response(websocket, chat_request_id)
            run_id = acknowledgement.get("runId")
            if not isinstance(run_id, str) or not run_id:
                raise OpenClawProtocolError("chat.send response is missing runId")
            while True:
                frame = await self._receive_json(websocket)
                if not _is_chat_frame_for_run(frame, run_id):
                    continue
                yield frame
                payload = frame["payload"]
                if isinstance(payload, dict) and payload["state"] in {"final", "aborted", "error"}:
                    return

    async def _authenticate(self, websocket: Any) -> None:
        challenge = await self._receive_json(websocket)
        parse_challenge(challenge)
        connect_id = self.make_id("connect")
        connect_request = build_connect_request(request_id=connect_id, token=self.token)
        await websocket.send(
            json.dumps(connect_request)
        )
        hello = await self._receive_response(websocket, connect_id)
        if hello.get("type") != "hello-ok" or hello.get("protocol") != OPENCLAW_PROTOCOL_VERSION:
            raise OpenClawProtocolError("OpenClaw negotiated an unsupported protocol")

    async def _receive_json(self, websocket: Any) -> dict[str, object]:
        raw = await asyncio.wait_for(websocket.recv(), self.timeout_seconds)
        if not isinstance(raw, str):
            raise OpenClawProtocolError("OpenClaw sent a binary protocol frame")
        try:
            frame = json.loads(raw)
        except json.JSONDecodeError as error:
            raise OpenClawProtocolError("OpenClaw sent invalid JSON") from error
        if not isinstance(frame, dict):
            raise OpenClawProtocolError("OpenClaw frame must be a JSON object")
        return frame

    async def _receive_response(self, websocket: Any, request_id: str) -> dict[str, object]:
        while True:
            frame = await self._receive_json(websocket)
            if frame.get("type") != "res" or frame.get("id") != request_id:
                continue
            if frame.get("ok") is not True:
                raise OpenClawProtocolError("OpenClaw request failed")
            payload = frame.get("payload")
            if not isinstance(payload, dict):
                raise OpenClawProtocolError("OpenClaw response payload must be an object")
            return payload


def _build_streaming_prompt(text: str) -> str:
    return (
        "你是 Sesame Robot 的口语对话代理。只输出自然语言中文回答；不要 JSON、Markdown、"
        "工具调用、表情说明或任何动作指令。回复应简短、自然，并直接回答用户。\n"
        f"用户：{text.strip()}"
    )


def _is_chat_frame_for_run(frame: dict[str, object], run_id: str) -> bool:
    if frame.get("type") != "event" or frame.get("event") != "chat":
        return False
    payload = frame.get("payload")
    return (
        isinstance(payload, dict)
        and payload.get("runId") == run_id
        and payload.get("state") in {"delta", "final", "aborted", "error"}
    )


def load_openclaw_gateway_token(config_path: Path) -> str:
    """Read the existing local Gateway token without copying it into the lab."""
    try:
        data = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("OpenClaw Gateway config is unavailable") from error
    gateway = data.get("gateway") if isinstance(data, dict) else None
    auth = gateway.get("auth") if isinstance(gateway, dict) else None
    token = auth.get("token") if isinstance(auth, dict) else None
    if not isinstance(token, str) or not token:
        raise ValueError("OpenClaw Gateway token is unavailable")
    return token


def main() -> None:
    """Run the lab-owned SSE bridge on a separate loopback port."""
    import uvicorn

    from sesame_voice_gateway.config import get_settings

    settings = get_settings()
    if settings.openclaw_token is None:
        raise ValueError("SESAME_OPENCLAW_TOKEN is required for the SSE bridge")
    upstream_token = load_openclaw_gateway_token(settings.openclaw_gateway_config_file)
    streamer = OpenClawWebSocketStreamer(
        url=settings.openclaw_url,
        token=upstream_token,
        timeout_seconds=settings.openclaw_timeout_seconds,
    )
    app = create_bridge_app(
        bridge_token=settings.openclaw_token.get_secret_value(),
        stream_openclaw=streamer,
    )
    uvicorn.run(app, host="127.0.0.1", port=settings.openclaw_sse_bridge_port)


class OpenClawChatEventMapper:
    """Map exactly one OpenClaw run without allowing text rewrites after output."""

    def __init__(self, *, turn_id: str, run_id: str) -> None:
        if not turn_id or not run_id:
            raise ValueError("turn_id and run_id are required")
        self._turn_id = turn_id
        self._run_id = run_id
        self._sequence = 0
        self._emitted_text = ""
        self._finished = False

    def feed(self, frame: dict[str, object]) -> BridgeSseEvent | None:
        if frame.get("type") != "event" or frame.get("event") != "chat":
            return None
        payload = frame.get("payload")
        if not isinstance(payload, dict) or payload.get("runId") != self._run_id:
            return None
        if self._finished:
            raise ValueError("OpenClaw emitted chat data after final")
        state = payload.get("state")
        if state == "delta":
            return self._delta(payload)
        if state == "final":
            self._finished = True
            return self._event("reply.final", {"expression": "idle", "actions": []})
        if state in {"aborted", "error"}:
            raise ValueError(f"OpenClaw run ended as {state}")
        raise ValueError("OpenClaw chat event has an unsupported state")

    def _delta(self, payload: dict[str, object]) -> BridgeSseEvent | None:
        text = payload.get("deltaText")
        if not isinstance(text, str):
            raise ValueError("OpenClaw delta text is invalid")
        if payload.get("replace") is True:
            if not text.startswith(self._emitted_text):
                raise ValueError("OpenClaw replacement would rewrite emitted text")
            text = text[len(self._emitted_text) :]
        if not text:
            return None
        self._emitted_text += text
        return self._event("reply.delta", {"text": text})

    def _event(self, event: str, data: dict[str, Any]) -> BridgeSseEvent:
        self._sequence += 1
        return BridgeSseEvent(
            event=event,
            data={"turn_id": self._turn_id, "seq": self._sequence, **data},
        )


def create_bridge_app(*, bridge_token: str, stream_openclaw: OpenClawFrameStream) -> FastAPI:
    """Create a loopback-hosted SSE façade around one OpenClaw chat run."""
    if not bridge_token:
        raise ValueError("bridge token is required")

    app = FastAPI()

    @app.get("/healthz")
    async def healthz() -> dict[str, str]:
        return {"status": "ok", "service": "sesame-openclaw-sse-bridge"}

    @app.post("/v1/sesame/reply-stream")
    async def reply_stream(
        request: Request,
        authorization: str | None = Header(default=None),
    ) -> StreamingResponse:
        if not _is_authorized(authorization, bridge_token):
            raise HTTPException(status_code=401, detail="invalid bridge authorization")
        try:
            body = await request.json()
        except json.JSONDecodeError as error:
            raise HTTPException(status_code=400, detail="request body must be JSON") from error
        bridge_request = _parse_bridge_request(body)

        async def events() -> AsyncIterator[str]:
            mapper: OpenClawChatEventMapper | None = None
            final_seen = False
            async for frame in stream_openclaw(
                agent_id=bridge_request.agent_id,
                session_key=bridge_request.session_key,
                text=bridge_request.text,
                request_id=bridge_request.request_id,
                turn_id=bridge_request.turn_id,
            ):
                if mapper is None:
                    run_id = _chat_run_id(frame)
                    if run_id is None:
                        continue
                    mapper = OpenClawChatEventMapper(
                        turn_id=bridge_request.turn_id,
                        run_id=run_id,
                    )
                output = mapper.feed(frame)
                if output is not None:
                    yield _serialize_sse(output)
                    final_seen = output.event == "reply.final"
            if mapper is None or not final_seen:
                raise RuntimeError("OpenClaw stream ended without reply.final")

        return StreamingResponse(
            events(),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-store", "X-Accel-Buffering": "no"},
        )

    return app


def _is_authorized(authorization: str | None, expected_token: str) -> bool:
    if authorization is None:
        return False
    scheme, separator, token = authorization.partition(" ")
    return separator == " " and scheme.lower() == "bearer" and hmac.compare_digest(token, expected_token)


def _parse_bridge_request(body: object) -> _BridgeRequest:
    if not isinstance(body, dict) or set(body) != {"agent_id", "session_key", "request"}:
        raise HTTPException(status_code=400, detail="request envelope is invalid")
    agent_id = body["agent_id"]
    session_key = body["session_key"]
    request = body["request"]
    if agent_id != "sesame" or not isinstance(session_key, str) or not session_key:
        raise HTTPException(status_code=400, detail="agent routing is invalid")
    if not isinstance(request, dict):
        raise HTTPException(status_code=400, detail="agent request is invalid")
    input_data = request.get("input")
    request_id = request.get("request_id")
    turn_id = request.get("turn_id")
    text = input_data.get("text") if isinstance(input_data, dict) else None
    if (
        request.get("v") != 1
        or not isinstance(request_id, str)
        or not request_id
        or not isinstance(turn_id, str)
        or not turn_id
        or not isinstance(text, str)
        or not text.strip()
    ):
        raise HTTPException(status_code=400, detail="agent request is invalid")
    return _BridgeRequest(
        agent_id=agent_id,
        session_key=session_key,
        request_id=request_id,
        turn_id=turn_id,
        text=text,
    )


def _chat_run_id(frame: dict[str, object]) -> str | None:
    if frame.get("type") != "event" or frame.get("event") != "chat":
        return None
    payload = frame.get("payload")
    run_id = payload.get("runId") if isinstance(payload, dict) else None
    return run_id if isinstance(run_id, str) and run_id else None


def _serialize_sse(event: BridgeSseEvent) -> str:
    data = json.dumps(event.data, ensure_ascii=False, separators=(",", ":"))
    return f"event: {event.event}\ndata: {data}\n\n"
