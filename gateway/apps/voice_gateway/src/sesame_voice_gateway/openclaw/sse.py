"""Strict, Gateway-owned contract for an OpenClaw reply SSE stream.

The Gateway accepts text only.  A stream cannot carry actions, device
identifiers, audio, or arbitrary JSON extensions.  That keeps the streaming
path on the same side of the hardware-control boundary as the v1 adapter.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from typing import Any


_IDENTIFIER = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,99}$")
_EXPRESSION = re.compile(r"^[a-z][a-z_]{0,63}$")
_SENTENCE_END = frozenset("。！？!?")
MAX_EVENT_BYTES = 16_384
MAX_EVENT_TEXT_CHARS = 1_024
MAX_TURN_TEXT_CHARS = 8_000


@dataclass(frozen=True, slots=True)
class ReplyDelta:
    text: str
    sequence: int


@dataclass(frozen=True, slots=True)
class ReplySentence:
    text: str
    sequence: int


@dataclass(frozen=True, slots=True)
class ReplyFinal:
    expression: str
    sequence: int


ReplyStreamOutput = ReplyDelta | ReplySentence | ReplyFinal


class SseReplyParser:
    """Validate one ordered reply stream and expose complete TTS sentences."""

    def __init__(self, *, expected_turn_id: str) -> None:
        if _IDENTIFIER.fullmatch(expected_turn_id) is None:
            raise ValueError("expected_turn_id is invalid")
        self._expected_turn_id = expected_turn_id
        self._previous_sequence = 0
        self._buffer = ""
        self._final_received = False

    def feed(self, *, event: str, data: str) -> tuple[ReplyStreamOutput, ...]:
        if self._final_received:
            raise ValueError("reply.final has already been received")
        if event not in {"reply.delta", "reply.sentence", "reply.final"}:
            raise ValueError("SSE event type is not supported")
        if len(data.encode("utf-8")) > MAX_EVENT_BYTES:
            raise ValueError("SSE event exceeds the byte limit")
        try:
            payload = json.loads(data)
        except json.JSONDecodeError as exc:
            raise ValueError("SSE event data is not valid JSON") from exc
        if not isinstance(payload, dict):
            raise ValueError("SSE event data must be a JSON object")

        sequence = self._validate_envelope(payload, event)
        if event == "reply.final":
            return self._finish(payload, sequence)
        return self._append_text(payload, sequence, force_boundary=event == "reply.sentence")

    def _validate_envelope(self, payload: dict[str, Any], event: str) -> int:
        allowed = (
            {"turn_id", "seq", "text"}
            if event != "reply.final"
            else {"turn_id", "seq", "expression", "actions"}
        )
        if set(payload) != allowed:
            raise ValueError("SSE event fields do not match its contract")
        turn_id = payload["turn_id"]
        sequence = payload["seq"]
        if turn_id != self._expected_turn_id:
            raise ValueError("SSE event turn_id does not match the active turn")
        if type(sequence) is not int or not 1 <= sequence <= 0xFFFFFFFF:
            raise ValueError("SSE event sequence is invalid")
        if sequence <= self._previous_sequence:
            raise ValueError("SSE event sequence is not strictly increasing")
        self._previous_sequence = sequence
        return sequence

    def _append_text(
        self, payload: dict[str, Any], sequence: int, *, force_boundary: bool
    ) -> tuple[ReplyStreamOutput, ...]:
        text = payload["text"]
        if not isinstance(text, str) or not text or len(text) > MAX_EVENT_TEXT_CHARS:
            raise ValueError("SSE reply text is invalid")
        self._buffer += text
        if len(self._buffer) > MAX_TURN_TEXT_CHARS:
            raise ValueError("SSE reply exceeds the turn text limit")
        outputs: list[ReplyStreamOutput] = [ReplyDelta(text=text, sequence=sequence)]
        outputs.extend(ReplySentence(text=sentence, sequence=sequence) for sentence in self._drain(force_boundary))
        return tuple(outputs)

    def _finish(self, payload: dict[str, Any], sequence: int) -> tuple[ReplyStreamOutput, ...]:
        expression = payload["expression"]
        actions = payload["actions"]
        if not isinstance(expression, str) or _EXPRESSION.fullmatch(expression) is None:
            raise ValueError("SSE final expression is invalid")
        if not isinstance(actions, list) or actions:
            raise ValueError("SSE final actions must be an empty array")
        self._final_received = True
        outputs = [ReplySentence(text=sentence, sequence=sequence) for sentence in self._drain(True)]
        outputs.append(ReplyFinal(expression=expression, sequence=sequence))
        return tuple(outputs)

    def _drain(self, force_boundary: bool) -> tuple[str, ...]:
        sentences: list[str] = []
        start = 0
        for index, character in enumerate(self._buffer):
            if character in _SENTENCE_END:
                sentence = self._buffer[start : index + 1].strip()
                if sentence:
                    sentences.append(sentence)
                start = index + 1
        self._buffer = self._buffer[start:]
        if force_boundary and self._buffer.strip():
            sentences.append(self._buffer.strip())
            self._buffer = ""
        return tuple(sentences)
