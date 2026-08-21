"""Loopback-only SSE adapter for the streaming OpenClaw lab contract."""

from __future__ import annotations

import asyncio
import uuid
from collections.abc import AsyncIterator, Callable, Iterator
from dataclasses import dataclass, field
from typing import Any
from urllib.parse import urlparse

import requests

from sesame_voice_gateway.openclaw.client import (
    OpenClawProtocolError,
    OpenClawUnavailableError,
    build_agent_request,
    build_openclaw_session_key,
)
from sesame_voice_gateway.openclaw.sse import MAX_EVENT_BYTES, ReplyStreamOutput, SseReplyParser


def _validate_loopback_sse_url(url: str) -> None:
    parsed = urlparse(url)
    if parsed.scheme != "http" or parsed.hostname not in {"127.0.0.1", "::1", "localhost"}:
        raise ValueError("OpenClaw SSE URL must use http on the local loopback interface")
    if parsed.username is not None or parsed.password is not None or not parsed.path:
        raise ValueError("OpenClaw SSE URL must not contain credentials and must include a path")


@dataclass(slots=True)
class _SseFrameDecoder:
    _event: str | None = None
    _data: str | None = None

    def feed(self, raw_line: str) -> tuple[str, str] | None:
        line = raw_line.rstrip("\r")
        if not line:
            if self._event is None and self._data is None:
                return None
            if self._event is None or self._data is None:
                raise OpenClawProtocolError("SSE record must include exactly one event and data field")
            event, data = self._event, self._data
            self._event = None
            self._data = None
            return event, data
        if line.startswith(":"):
            return None
        name, separator, value = line.partition(":")
        if not separator or name not in {"event", "data"}:
            raise OpenClawProtocolError("SSE field is not allowed")
        if value.startswith(" "):
            value = value[1:]
        if len(value.encode("utf-8")) > MAX_EVENT_BYTES:
            raise OpenClawProtocolError("SSE field exceeds the byte limit")
        if name == "event":
            if self._event is not None:
                raise OpenClawProtocolError("SSE record has more than one event field")
            self._event = value
        else:
            if self._data is not None:
                raise OpenClawProtocolError("SSE record has more than one data field")
            self._data = value
        return None


def _next_line(lines: Iterator[str]) -> str | None:
    try:
        return next(lines)
    except StopIteration:
        return None


@dataclass(slots=True)
class OpenClawSseAgentProvider:
    """Request a sanitized reply stream from a local OpenClaw SSE bridge."""

    url: str
    token: str
    agent_id: str = "sesame"
    session_key_secret: str = ""
    timeout_seconds: float = 60.0
    post: Callable[..., Any] = field(default=requests.post, repr=False)

    def __post_init__(self) -> None:
        _validate_loopback_sse_url(self.url)
        if not self.token or not self.session_key_secret:
            raise ValueError("OpenClaw SSE token and session key secret are required")
        if self.timeout_seconds <= 0:
            raise ValueError("OpenClaw SSE timeout must be positive")

    async def stream_reply(
        self,
        *,
        text: str,
        device_id: str,
        user_id: str,
        conversation_id: str,
        turn_id: str,
    ) -> AsyncIterator[ReplyStreamOutput]:
        # The upstream SSE bridge receives only the fixed agent schema. Device
        # and user identifiers remain Gateway-internal routing state.
        del device_id, user_id
        request_id = f"req_{uuid.uuid4().hex}"
        session_key = build_openclaw_session_key(
            agent_id=self.agent_id,
            conversation_id=conversation_id,
            session_key_secret=self.session_key_secret,
        )
        request = build_agent_request(
            request_id=request_id,
            conversation_id=conversation_id,
            turn_id=turn_id,
            text=text,
        )
        response = await self._open_stream(
            {"agent_id": self.agent_id, "session_key": session_key, "request": request}
        )
        try:
            parser = SseReplyParser(expected_turn_id=turn_id)
            decoder = _SseFrameDecoder()
            lines = response.iter_lines(decode_unicode=True)
            while (line := await asyncio.to_thread(_next_line, lines)) is not None:
                if not isinstance(line, str):
                    raise OpenClawProtocolError("SSE response must be UTF-8 text")
                record = decoder.feed(line)
                if record is None:
                    continue
                event, data = record
                for output in parser.feed(event=event, data=data):
                    yield output
            if decoder._event is not None or decoder._data is not None:
                raise OpenClawProtocolError("SSE response ended before its record boundary")
        except (requests.RequestException, OSError, TimeoutError) as exc:
            raise OpenClawUnavailableError("OpenClaw SSE stream is unavailable") from exc
        finally:
            await asyncio.to_thread(response.close)

    async def _open_stream(self, payload: dict[str, Any]) -> Any:
        try:
            response = await asyncio.to_thread(
                self.post,
                url=self.url,
                headers={"Authorization": f"Bearer {self.token}", "Accept": "text/event-stream"},
                json=payload,
                stream=True,
                timeout=(5.0, self.timeout_seconds),
                allow_redirects=False,
            )
            response.raise_for_status()
        except (requests.RequestException, OSError, TimeoutError) as exc:
            raise OpenClawUnavailableError("OpenClaw SSE stream is unavailable") from exc
        content_type = response.headers.get("content-type", "")
        if not isinstance(content_type, str) or not content_type.lower().startswith("text/event-stream"):
            response.close()
            raise OpenClawProtocolError("OpenClaw response is not an SSE stream")
        return response
