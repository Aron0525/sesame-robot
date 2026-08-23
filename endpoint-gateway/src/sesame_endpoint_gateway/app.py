"""Device WSS entry point and local operator console for Sesame Robot V3.

The browser never connects to a robot directly.  It connects to the local
gateway console, which owns the current device session and validates every
control message before forwarding it to the device WSS stream.
"""

from __future__ import annotations

import asyncio
import hmac
import json
import time
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from pathlib import Path
from secrets import token_urlsafe
from typing import AsyncIterator, Callable, Literal, Mapping, Optional, Protocol

from fastapi import FastAPI, Header, HTTPException, Request, WebSocket, WebSocketDisconnect, status
from fastapi.responses import FileResponse, JSONResponse, StreamingResponse
from pydantic import BaseModel, ConfigDict, Field, ValidationError

from sesame_endpoint_gateway.protocol import DOWNLINK, AudioFrameError, UPLINK, pack_audio_frame, unpack_audio_frame
from sesame_endpoint_gateway.scenes import DEFAULT_SCENE_ID, SCENES, active_profile, is_mode_id, scene_catalog, scene_profile
from sesame_endpoint_gateway.tts import MacSayTtsSynthesizer, OpusEncoder, OpusPacketEncoder, TtsError, TtsSynthesizer


STATIC_DIR = Path(__file__).with_name("static")
ALLOWED_ACTIONS = frozenset({"rest", "stand", "wave", "stop"})
ALLOWED_EXPRESSIONS = frozenset({"default", "happy", "thinking"})
# The operator console has a deliberately broader, still finite, catalogue.
# OpenClaw plans continue to use the smaller policy above; a browser command
# must never become an arbitrary string executed by the ESP32.
LEGACY_ACTIONS = frozenset(
    {
        "rest", "stand", "wave", "dance", "swim", "point", "pushup",
        "bow", "cute", "freaky", "worm", "shake", "shrug", "dead",
        "crab", "forward", "backward", "left", "right",
    }
)
LEGACY_EXPRESSIONS = frozenset(
    {
        "default", "idle", "walk", "rest", "swim", "dance", "wave",
        "point", "stand", "cute", "pushup", "freaky", "bow", "worm",
        "shake", "shrug", "dead", "crab", "happy", "talk_happy", "sad",
        "talk_sad", "angry", "talk_angry", "surprised", "talk_surprised",
        "sleepy", "talk_sleepy", "love", "talk_love", "excited",
        "talk_excited", "confused", "talk_confused", "thinking",
        "talk_thinking",
    }
)
MAX_ACTION_DURATION_MS = 10_000
MAX_EXPRESSION_TTL_MS = 10_000
DEFAULT_ACTION_DURATION_MS = {"rest": 1_500, "stand": 1_500, "wave": 2_000, "stop": 100}
DEFAULT_EXPRESSION_TTL_MS = 3_000


class ServiceAdvertiser(Protocol):
    def start(self) -> None: ...

    def stop(self) -> None: ...


class AudioContract(BaseModel):
    model_config = ConfigDict(extra="forbid")

    codec: str
    sample_rate: int
    channels: int
    frame_duration_ms: int

    def is_supported(self) -> bool:
        return (
            self.codec == "opus"
            and self.sample_rate == 16000
            and self.channels == 1
            and self.frame_duration_ms == 20
        )


class SessionHelloPayload(BaseModel):
    model_config = ConfigDict(extra="forbid")

    device_id: str = Field(min_length=1, max_length=100)
    gateway_id: str = Field(min_length=1, max_length=100)
    conversation_id: Optional[str] = Field(default=None, max_length=100)
    protocol_version: int
    audio: AudioContract


class SceneSelectionRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    device_id: str = Field(min_length=1, max_length=100)
    scene_id: str = Field(min_length=1, max_length=32)


class AgentReply(BaseModel):
    model_config = ConfigDict(extra="forbid")

    text: str = Field(min_length=1, max_length=2_000)


class AgentVoice(BaseModel):
    model_config = ConfigDict(extra="forbid")

    voice_id: str = Field(min_length=1, max_length=100)
    style: Literal["neutral", "happy", "sad", "angry", "surprised", "thinking"]
    speed: float = Field(ge=0.5, le=2.0)


class AgentExpression(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: Literal["default", "happy", "thinking"]
    ttl_ms: int = Field(ge=100, le=MAX_EXPRESSION_TTL_MS)


class AgentAction(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: Literal["rest", "stand", "wave", "stop"]
    duration_ms: int = Field(ge=100, le=MAX_ACTION_DURATION_MS)


class OpenClawAgentResponse(BaseModel):
    """The completed structured result of one ASR-driven OpenClaw turn."""

    model_config = ConfigDict(extra="forbid")

    v: Literal[1]
    request_id: str = Field(min_length=1, max_length=100)
    turn_id: str = Field(min_length=1, max_length=100)
    status: Literal["completed"]
    reply: AgentReply
    voice: AgentVoice
    expression: AgentExpression
    actions: list[AgentAction] = Field(max_length=1)


class OpenClawFeedbackRequest(BaseModel):
    """Bridge payload posted after OpenClaw interprets an ASR transcript."""

    model_config = ConfigDict(extra="forbid")

    device_id: str = Field(min_length=1, max_length=100)
    agent_id: str = Field(min_length=1, max_length=100)
    response: OpenClawAgentResponse


class ControlEvent(BaseModel):
    model_config = ConfigDict(extra="forbid")

    v: int
    type: str
    session_id: Optional[str] = Field(default=None, max_length=100)
    turn_id: Optional[str] = Field(default=None, max_length=100)
    request_id: Optional[str] = Field(default=None, max_length=100)
    sequence: int = Field(ge=0)
    timestamp_ms: int = Field(ge=0)
    payload: dict


@dataclass
class DeviceSession:
    device_id: str
    gateway_id: str
    websocket: WebSocket
    session_id: str = field(default_factory=lambda: f"ses_{token_urlsafe(12)}")
    next_server_sequence: int = 0
    next_tts_generation: int = 1
    latest_status: dict = field(default_factory=dict)
    scene_id: str = DEFAULT_SCENE_ID

    def server_event(
        self,
        event_type: str,
        payload: dict,
        *,
        request_id: Optional[str] = None,
        turn_id: Optional[str] = None,
    ) -> dict:
        event = {
            "v": 1,
            "type": event_type,
            "session_id": self.session_id,
            "turn_id": turn_id,
            "request_id": request_id,
            "sequence": self.next_server_sequence,
            "timestamp_ms": int(time.time() * 1000),
            "payload": payload,
        }
        self.next_server_sequence += 1
        return event

    def ready_event(self, conversation_id: Optional[str]) -> dict:
        return self.server_event(
            "session.ready",
            {
                "gateway_id": self.gateway_id,
                "conversation_id": conversation_id,
                "protocol_version": 1,
                "audio": {
                    "codec": "opus",
                    "sample_rate": 16000,
                    "channels": 1,
                    "frame_duration_ms": 20,
                },
            },
        )


class GatewayHub:
    """In-memory registry for a local, single-operator development gateway."""

    def __init__(self) -> None:
        self.devices: dict[str, DeviceSession] = {}
        self.consoles: set[WebSocket] = set()
        self.scene_by_device: dict[str, str] = {}
        self.legacy_events: list[dict] = []
        self.next_legacy_event_id = 1

    @property
    def active_device(self) -> Optional[DeviceSession]:
        return next(iter(self.devices.values()), None) if len(self.devices) == 1 else None

    def scene_for(self, device_id: str) -> dict:
        return active_profile(self.scene_by_device.get(device_id, DEFAULT_SCENE_ID))

    async def select_scene(self, device_id: str, scene_id: str, *, source: str) -> dict:
        if scene_id not in SCENES:
            raise ValueError(f"unknown_scene:{scene_id}")
        return await self.select_mode(device_id, scene_id, source=source)

    async def select_mode(self, device_id: str, mode_id: str, *, source: str) -> dict:
        if not is_mode_id(mode_id):
            raise ValueError(f"unknown_mode:{mode_id}")
        scene = active_profile(mode_id)
        self.scene_by_device[device_id] = mode_id
        session = self.devices.get(device_id)
        if session is not None:
            session.scene_id = mode_id
        payload = {"device_id": device_id, "scene": scene, "source": source}
        await self.broadcast({"type": "scene.changed", "payload": payload})
        return payload

    async def broadcast(self, message: dict) -> None:
        closed: list[WebSocket] = []
        for console in tuple(self.consoles):
            try:
                await console.send_json(message)
            except (RuntimeError, WebSocketDisconnect):
                closed.append(console)
        for console in closed:
            self.consoles.discard(console)

    async def publish_snapshot(self, console: WebSocket) -> None:
        device = self.active_device
        await console.send_json(
            {
                "type": "console.snapshot",
                "payload": {
                    "device_id": device.device_id if device else None,
                    "connected": device is not None,
                    "status": device.latest_status if device else {},
                    "scene": self.scene_for(device.device_id) if device else None,
                },
            }
        )

    async def publish_presence(self) -> None:
        device = self.active_device
        await self.broadcast(
            {
                "type": "device.presence",
                "payload": {"device_id": device.device_id if device else None, "connected": device is not None},
            }
        )

    def record_legacy_action_result(self, device_id: str, event: ControlEvent) -> None:
        """Keep the browser's HTTP control surface tied to a device result."""
        payload = event.payload if isinstance(event.payload, dict) else {}
        record = {
            "id": self.next_legacy_event_id,
            "device_id": device_id,
            "turn_id": event.turn_id,
            "stage": "device.action",
            "status": payload.get("status", "unknown"),
            "timestamp_ms": event.timestamp_ms,
            "details": {
                "request_id": event.request_id,
                "error_code": payload.get("error_code"),
            },
        }
        self.next_legacy_event_id += 1
        self.legacy_events.append(record)
        if len(self.legacy_events) > 80:
            del self.legacy_events[:-80]


def bearer_token(header: Optional[str]) -> Optional[str]:
    if header is None or not header.startswith("Bearer "):
        return None
    token = header[7:]
    return token if token else None


def console_error(code: str, request_id: Optional[str]) -> dict:
    return {
        "type": "command.rejected",
        "request_id": request_id,
        "payload": {"code": code},
    }


def validate_legacy_control(raw: object) -> tuple[Optional[dict], Optional[str]]:
    """Validate the original web-controller contract before WSS forwarding."""
    if not isinstance(raw, dict):
        return None, "invalid_command"
    kind = raw.get("kind")
    if not isinstance(kind, str):
        return None, "invalid_kind"

    expected_keys = {
        "action": {"kind", "action"},
        "expression": {"kind", "expression"},
        "servo": {"kind", "servo", "angle"},
        "settings": {
            "kind", "frame_delay_ms", "walk_cycles", "motor_current_delay_ms"
        },
        "wakeword_settings": {"kind", "wake_threshold_hundredths"},
        "stop": {"kind"},
    }
    allowed_keys = expected_keys.get(kind)
    if allowed_keys is None or set(raw) != allowed_keys:
        return None, f"invalid_{kind}" if kind else "invalid_kind"

    if kind == "action":
        return (dict(raw), None) if raw["action"] in LEGACY_ACTIONS else (None, "unknown_action")
    if kind == "expression":
        return (
            (dict(raw), None)
            if raw["expression"] in LEGACY_EXPRESSIONS
            else (None, "unknown_expression")
        )
    if kind == "servo":
        servo, angle = raw["servo"], raw["angle"]
        if type(servo) is not int or type(angle) is not int or not 1 <= servo <= 8 or not 0 <= angle <= 180:
            return None, "invalid_servo"
    elif kind == "settings":
        frame_delay = raw["frame_delay_ms"]
        cycles = raw["walk_cycles"]
        motor_delay = raw["motor_current_delay_ms"]
        if (
            type(frame_delay) is not int
            or type(cycles) is not int
            or type(motor_delay) is not int
            or not 10 <= frame_delay <= 1_000
            or not 1 <= cycles <= 50
            or not 0 <= motor_delay <= 500
        ):
            return None, "invalid_settings"
    elif kind == "wakeword_settings":
        threshold = raw["wake_threshold_hundredths"]
        if type(threshold) is not int or not 5 <= threshold <= 95:
            return None, "invalid_wakeword_settings"
    return dict(raw), None


def validate_console_command(raw: object) -> tuple[Optional[str], Optional[dict], Optional[dict]]:
    """Return device event type, payload, and an error message for a web command."""
    if not isinstance(raw, dict):
        return None, None, console_error("invalid_command", None)
    command_type = raw.get("type")
    request_id = raw.get("request_id")
    payload = raw.get("payload")
    if not isinstance(request_id, str) or not request_id or len(request_id) > 100:
        return None, None, console_error("invalid_request_id", None)
    if not isinstance(payload, dict):
        return None, None, console_error("invalid_payload", request_id)
    if command_type == "scene.select":
        scene_id = payload.get("scene_id")
        if scene_id not in SCENES:
            return None, None, console_error("unknown_scene", request_id)
        return "scene.select", {"scene_id": scene_id}, None
    if command_type == "mode.select":
        mode_id = payload.get("mode_id")
        if not is_mode_id(mode_id):
            return None, None, console_error("unknown_mode", request_id)
        return "mode.select", {"mode_id": mode_id}, None
    if command_type == "robot.stop":
        return "action.execute", {"action": "stop", "duration_ms": DEFAULT_ACTION_DURATION_MS["stop"]}, None
    if command_type == "action.execute":
        action = payload.get("action")
        if action not in ALLOWED_ACTIONS:
            return None, None, console_error("unknown_action", request_id)
        duration = payload.get("duration_ms", DEFAULT_ACTION_DURATION_MS[action])
        if type(duration) is not int or duration <= 0 or duration > MAX_ACTION_DURATION_MS:
            return None, None, console_error("invalid_duration", request_id)
        return "action.execute", {"action": action, "duration_ms": duration}, None
    if command_type == "expression.set":
        expression = payload.get("expression")
        if expression not in ALLOWED_EXPRESSIONS:
            return None, None, console_error("unknown_expression", request_id)
        ttl = payload.get("ttl_ms", DEFAULT_EXPRESSION_TTL_MS)
        if type(ttl) is not int or ttl < 100 or ttl > MAX_EXPRESSION_TTL_MS:
            return None, None, console_error("invalid_expression_ttl", request_id)
        return "expression.set", {"expression": expression, "ttl_ms": ttl}, None
    return None, None, console_error("unsupported_command", request_id)


def create_app(
    *,
    device_tokens: Mapping[str, str],
    gateway_id: str = "sesame-edge",
    scene_control_token: Optional[str] = None,
    tts_synthesizer: Optional[TtsSynthesizer] = None,
    opus_encoder_factory: Optional[Callable[[], OpusEncoder]] = None,
    mdns_advertiser: Optional[ServiceAdvertiser] = None,
) -> FastAPI:
    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        if mdns_advertiser is not None:
            await asyncio.to_thread(mdns_advertiser.start)
        try:
            yield
        finally:
            if mdns_advertiser is not None:
                await asyncio.to_thread(mdns_advertiser.stop)

    app = FastAPI(title="Sesame Endpoint Voice Gateway", version="0.2.0", lifespan=lifespan)
    accepted_tokens = frozenset(device_tokens.values())
    hub = GatewayHub()
    app.state.hub = hub
    tts = tts_synthesizer or MacSayTtsSynthesizer()
    make_opus_encoder = opus_encoder_factory or OpusPacketEncoder

    @app.get("/", include_in_schema=False)
    async def console_page() -> FileResponse:
        return FileResponse(STATIC_DIR / "index.html", media_type="text/html")

    @app.get("/console", include_in_schema=False)
    async def legacy_console_page() -> FileResponse:
        return FileResponse(
            STATIC_DIR / "legacy-console.html",
            media_type="text/html",
            headers={"Cache-Control": "no-store"},
        )

    def legacy_observability_snapshot() -> dict:
        now_ms = int(time.time() * 1_000)
        devices = [
            {
                "device_id": device.device_id,
                "online": True,
                "session_id": device.session_id,
                "current_turn_id": device.latest_status.get("turn_id"),
                "current_stage": device.latest_status.get("stage", "session"),
                "updated_at_ms": now_ms,
            }
            for device in hub.devices.values()
        ]
        return {
            "devices": devices,
            "turns": [],
            "events": list(hub.legacy_events),
            "debug_content": False,
        }

    @app.get("/api/observability/snapshot", include_in_schema=False)
    async def legacy_observability_snapshot_page() -> JSONResponse:
        return JSONResponse(legacy_observability_snapshot(), headers={"Cache-Control": "no-store"})

    @app.get("/api/observability/stream", include_in_schema=False)
    async def legacy_observability_stream(request: Request) -> StreamingResponse:
        async def snapshots() -> AsyncIterator[str]:
            while not await request.is_disconnected():
                payload = json.dumps(legacy_observability_snapshot(), ensure_ascii=False)
                yield f"event: snapshot\ndata: {payload}\n\n"
                await asyncio.sleep(1)

        return StreamingResponse(
            snapshots(),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-store", "X-Accel-Buffering": "no"},
        )

    @app.post("/api/local-control/{device_id}", include_in_schema=False)
    async def legacy_local_control(device_id: str, request: Request) -> JSONResponse:
        try:
            command = await request.json()
        except (TypeError, ValueError) as exc:
            raise HTTPException(status_code=422, detail="invalid_command") from exc
        if not isinstance(command, dict):
            raise HTTPException(status_code=422, detail="invalid_command")

        request_id = f"ctl_{token_urlsafe(10)}"
        payload, error = validate_legacy_control(command)
        if error is not None:
            raise HTTPException(status_code=422, detail=error)
        device = hub.devices.get(device_id)
        if device is None:
            raise HTTPException(status_code=409, detail="device_offline")
        try:
            await device.websocket.send_json(
                device.server_event("operator.control", payload, request_id=request_id)
            )
        except (RuntimeError, WebSocketDisconnect) as exc:
            raise HTTPException(status_code=409, detail="device_offline") from exc
        return JSONResponse(
            {
                "status": "accepted",
                "device_id": device_id,
                "request_id": request_id,
                "error_code": None,
            },
            headers={"Cache-Control": "no-store"},
        )

    @app.get("/v1/openclaw/scenes")
    async def openclaw_scene_catalog(authorization: Optional[str] = Header(default=None)) -> dict:
        if scene_control_token is None:
            raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="scene_control_unconfigured")
        if not hmac.compare_digest(bearer_token(authorization) or "", scene_control_token):
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid_scene_control_token")
        return {"scenes": scene_catalog()}

    @app.get("/v1/openclaw/scene/{device_id}")
    async def openclaw_current_scene(
        device_id: str, authorization: Optional[str] = Header(default=None)
    ) -> dict:
        if scene_control_token is None:
            raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="scene_control_unconfigured")
        if not hmac.compare_digest(bearer_token(authorization) or "", scene_control_token):
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid_scene_control_token")
        if not device_id or len(device_id) > 100:
            raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_CONTENT, detail="invalid_device_id")
        return {"device_id": device_id, "scene": hub.scene_for(device_id)}

    @app.post("/v1/openclaw/scene")
    async def select_openclaw_scene(
        request: SceneSelectionRequest, authorization: Optional[str] = Header(default=None)
    ) -> dict:
        if scene_control_token is None:
            raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="scene_control_unconfigured")
        if not hmac.compare_digest(bearer_token(authorization) or "", scene_control_token):
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid_scene_control_token")
        try:
            changed = await hub.select_scene(request.device_id, request.scene_id, source="openclaw")
        except ValueError:
            raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_CONTENT, detail="unknown_scene")
        return changed

    @app.post("/v1/openclaw/feedback")
    async def forward_openclaw_feedback(
        request: OpenClawFeedbackRequest, authorization: Optional[str] = Header(default=None)
    ) -> dict:
        """Forward one validated model-selected expression to a device.

        The ASR and model adapters stay outside this gateway.  They post this
        completed result only after the active OpenClaw agent has interpreted
        the transcript.  Checking agent_id against the current mode prevents a
        result from a previously active agent reaching the robot after a switch.
        Actions remain accepted for response-schema compatibility but are not
        forwarded to the device.
        """
        if scene_control_token is None:
            raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="scene_control_unconfigured")
        if not hmac.compare_digest(bearer_token(authorization) or "", scene_control_token):
            raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid_scene_control_token")

        device = hub.devices.get(request.device_id)
        if device is None:
            raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="device_unavailable")
        active_agent_id = hub.scene_for(request.device_id)["agent_id"]
        if request.agent_id != active_agent_id:
            raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail="stale_agent_response")

        response = request.response
        try:
            pcm = await tts.synthesize(response.reply.text, speed=response.voice.speed)
            opus_packets = make_opus_encoder().encode_pcm(pcm)
        except TtsError as exc:
            raise HTTPException(status_code=status.HTTP_502_BAD_GATEWAY, detail="tts_unavailable") from exc

        feedback: list[tuple[str, dict]] = [
            (
                "expression.set",
                {"expression": response.expression.name, "ttl_ms": response.expression.ttl_ms},
            )
        ]
        generation_id = device.next_tts_generation
        device.next_tts_generation += 1
        try:
            for event_type, payload in feedback:
                await device.websocket.send_json(
                    device.server_event(
                        event_type,
                        payload,
                        request_id=response.request_id,
                        turn_id=response.turn_id,
                    )
                )
            await device.websocket.send_json(
                device.server_event(
                    "tts.start",
                    {"generation_id": generation_id},
                    request_id=response.request_id,
                    turn_id=response.turn_id,
                )
            )
            started_at_ms = int(time.time() * 1000)
            for sequence, packet in enumerate(opus_packets):
                await device.websocket.send_bytes(
                    pack_audio_frame(
                        direction=DOWNLINK,
                        flags=0,
                        stream_id=generation_id,
                        generation_id=generation_id,
                        sequence=sequence,
                        timestamp_ms=started_at_ms + sequence * 20,
                        payload=packet,
                    )
                )
                if sequence + 1 >= 30 and sequence + 1 < len(opus_packets):
                    await asyncio.sleep(0.02)
            await device.websocket.send_json(
                device.server_event(
                    "tts.stop",
                    {"generation_id": generation_id},
                    request_id=response.request_id,
                    turn_id=response.turn_id,
                )
            )
        except (RuntimeError, WebSocketDisconnect):
            hub.devices.pop(device.device_id, None)
            await hub.publish_presence()
            raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="device_unavailable")

        return {
            "device_id": request.device_id,
            "agent_id": request.agent_id,
            "request_id": response.request_id,
            "turn_id": response.turn_id,
            "forwarded": [event_type for event_type, _ in feedback] + ["tts.start", "audio", "tts.stop"],
            "ignored_actions": len(response.actions),
        }

    @app.get("/healthz")
    async def healthz() -> dict[str, str]:
        return {"status": "ok"}

    @app.websocket("/v1/console")
    async def console(websocket: WebSocket) -> None:
        await websocket.accept()
        hub.consoles.add(websocket)
        await hub.publish_snapshot(websocket)
        try:
            while True:
                raw = await websocket.receive_json()
                event_type, payload, error = validate_console_command(raw)
                if error is not None:
                    await websocket.send_json(error)
                    continue
                device = hub.active_device
                request_id = raw["request_id"]
                if event_type in {"scene.select", "mode.select"}:
                    if device is None:
                        await websocket.send_json(console_error("device_unavailable", request_id))
                        continue
                    if event_type == "scene.select":
                        changed = await hub.select_scene(device.device_id, payload["scene_id"], source="console")
                    else:
                        changed = await hub.select_mode(device.device_id, payload["mode_id"], source="console")
                    await websocket.send_json(
                        {"type": "command.accepted", "request_id": request_id, "payload": {"command": event_type, **changed}}
                    )
                    continue
                if device is None:
                    await websocket.send_json(console_error("device_unavailable", request_id))
                    continue
                device_payload = dict(payload)
                if event_type == "action.execute":
                    device_payload["deadline_ms"] = int(time.time() * 1000) + max(
                        5_000, device_payload["duration_ms"] + 2_000
                    )
                event = device.server_event(event_type, device_payload, request_id=request_id)
                try:
                    await device.websocket.send_json(event)
                except (RuntimeError, WebSocketDisconnect):
                    hub.devices.pop(device.device_id, None)
                    await hub.publish_presence()
                    await websocket.send_json(console_error("device_unavailable", request_id))
                    continue
                await websocket.send_json(
                    {"type": "command.accepted", "request_id": request_id, "payload": {"command": event_type}}
                )
        except WebSocketDisconnect:
            pass
        finally:
            hub.consoles.discard(websocket)

    @app.websocket("/v1/device-stream")
    @app.websocket("/v2/device-stream")
    async def device_stream(websocket: WebSocket) -> None:
        token = bearer_token(websocket.headers.get("authorization"))
        if token is None or token not in accepted_tokens:
            await websocket.close(code=status.WS_1008_POLICY_VIOLATION)
            return

        await websocket.accept()
        try:
            raw_hello = await websocket.receive_json()
            hello_event = ControlEvent.model_validate(raw_hello)
            hello = SessionHelloPayload.model_validate(hello_event.payload)
        except (ValidationError, TypeError, ValueError):
            await websocket.close(code=status.WS_1003_UNSUPPORTED_DATA)
            return

        if (
            hello_event.v != 1
            or hello_event.type != "session.hello"
            or hello_event.sequence != 0
            or not hello.audio.is_supported()
            or hello.protocol_version != 1
            or hello.gateway_id != gateway_id
            or device_tokens.get(hello.device_id) != token
        ):
            await websocket.close(code=status.WS_1008_POLICY_VIOLATION)
            return

        session = DeviceSession(hello.device_id, gateway_id, websocket, scene_id=hub.scene_for(hello.device_id)["id"])
        existing = hub.devices.get(session.device_id)
        hub.devices[session.device_id] = session
        await websocket.send_json(session.ready_event(hello.conversation_id))
        await hub.publish_presence()
        try:
            while True:
                message = await websocket.receive()
                text = message.get("text")
                binary = message.get("bytes")
                if text is not None:
                    try:
                        event = ControlEvent.model_validate_json(text)
                    except ValidationError:
                        continue
                    if event.type == "status.update":
                        session.latest_status = event.payload
                        await hub.broadcast(
                            {
                                "type": "status.update",
                                "device_id": session.device_id,
                                "payload": event.payload,
                            }
                        )
                    elif event.type == "action.result":
                        hub.record_legacy_action_result(session.device_id, event)
                        await hub.broadcast(
                            {
                                "type": "action.result",
                                "device_id": session.device_id,
                                "request_id": event.request_id,
                                "payload": event.payload,
                            }
                        )
                    continue
                if binary is None:
                    continue
                try:
                    unpack_audio_frame(binary, expected_direction=UPLINK)
                except AudioFrameError:
                    await websocket.close(code=status.WS_1003_UNSUPPORTED_DATA)
                    return
        except (RuntimeError, WebSocketDisconnect):
            return
        finally:
            if hub.devices.get(session.device_id) is session:
                hub.devices.pop(session.device_id, None)
                await hub.publish_presence()
            if existing is not None and existing is not session:
                # The new session is authoritative; the old socket is allowed to close naturally.
                pass

    return app
