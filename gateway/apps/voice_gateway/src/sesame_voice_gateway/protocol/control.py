from __future__ import annotations

import json
from functools import lru_cache
from typing import Any, Literal

from jsonschema import Draft202012Validator
from pydantic import BaseModel, ConfigDict, Field, ValidationError

from sesame_voice_gateway.schema_resources import load_schema

MAX_CONTROL_FRAME_BYTES = 16_384
ControlEventType = Literal[
    "session.hello",
    "session.ready",
    "listen.start",
    "listen.stop",
    "interrupt",
    "asr.partial",
    "asr.final",
    "agent.reply",
    "response.plan",
    "turn.complete",
    "tts.start",
    "tts.stop",
    "tts.flush",
    "expression.set",
    "action.execute",
    "operator.control",
    "action.result",
    "error",
]


class ControlProtocolError(ValueError):
    """Raised when a JSON control frame violates the v1 contract."""


class ControlEvent(BaseModel):
    model_config = ConfigDict(extra="forbid", frozen=True)

    v: Literal[1]
    type: ControlEventType
    session_id: str | None = Field(max_length=100)
    turn_id: str | None = Field(max_length=100)
    request_id: str | None = Field(max_length=100)
    sequence: int = Field(ge=0, le=0xFFFFFFFF)
    timestamp_ms: int = Field(ge=0)
    payload: dict[str, Any]


@lru_cache(maxsize=1)
def _control_validator() -> Draft202012Validator:
    return Draft202012Validator(load_schema("control-event.v1.schema.json"))


def parse_control_event(message: str | bytes) -> ControlEvent:
    raw = message.encode("utf-8") if isinstance(message, str) else message
    if len(raw) > MAX_CONTROL_FRAME_BYTES:
        raise ControlProtocolError("control frame is too large")

    try:
        data = json.loads(raw)
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ControlProtocolError("control frame is not valid UTF-8 JSON") from exc

    errors = sorted(_control_validator().iter_errors(data), key=lambda error: list(error.path))
    if errors:
        raise ControlProtocolError(errors[0].message)

    try:
        return ControlEvent.model_validate(data)
    except ValidationError as exc:
        raise ControlProtocolError(str(exc)) from exc


def serialize_control_event(event: ControlEvent) -> str:
    return event.model_dump_json(exclude_none=False)
