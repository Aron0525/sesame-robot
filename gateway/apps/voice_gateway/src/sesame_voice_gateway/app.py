from __future__ import annotations

import asyncio
import logging
import secrets
import socket
import time
import uuid
from collections.abc import AsyncIterator, Callable
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from importlib.resources import files
from ipaddress import ip_address
import json
from typing import Any, Literal, Protocol

from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, JSONResponse, StreamingResponse
from pydantic import BaseModel, ConfigDict, model_validator

from sesame_voice_gateway.audio.opus import OpusCodec
from sesame_voice_gateway.config import Settings, get_settings
from sesame_voice_gateway.conversations import ConversationRegistry
from sesame_voice_gateway.discovery import MdnsAdvertiser
from sesame_voice_gateway.openclaw.client import (
    OpenClawAgentProvider,
    OpenClawUnavailableError,
    build_openclaw_session_key,
)
from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.pipeline import (
    MAX_UPLINK_PACKETS,
    ConversationContext,
    ConversationPipeline,
    TurnResult,
)
from sesame_voice_gateway.policy import PolicyViolation
from sesame_voice_gateway.privacy import require_remote_speech_consent
from sesame_voice_gateway.protocol.audio import (
    AudioDirection,
    AudioEncoding,
    AudioFrame,
    AudioProtocolError,
    MAX_OPUS_PACKET_BYTES,
    pack_audio_frame,
    unpack_audio_frame,
)
from sesame_voice_gateway.protocol.control import (
    ControlEvent,
    ControlProtocolError,
    parse_control_event,
    serialize_control_event,
)
from sesame_voice_gateway.providers.base import AgentProvider, AsrProvider, AudioFormat, TtsProvider
from sesame_voice_gateway.providers.dashscope import (
    DashScopeAsrProvider,
    DashScopeAudioClient,
    DashScopeTtsProvider,
)
from sesame_voice_gateway.recordings import create_test_recording_store
from sesame_voice_gateway.sandbox_cleanup import OpenClawSandboxReaper
from sesame_voice_gateway.serial_monitor import FirmwareSerialMonitor
from sesame_voice_gateway.tools.web_search import DashScopeWebSearchProvider, WebSearchProvider

logger = logging.getLogger(__name__)

MAX_UPLINK_OPUS_BYTES = MAX_UPLINK_PACKETS * MAX_OPUS_PACKET_BYTES
REMOTE_ACTIONS = frozenset(
    {
        "rest",
        "stand",
        "wave",
        "dance",
        "swim",
        "point",
        "pushup",
        "bow",
        "cute",
        "freaky",
        "worm",
        "shake",
        "shrug",
        "dead",
        "crab",
        "forward",
        "backward",
        "left",
        "right",
    }
)
REMOTE_EXPRESSIONS = frozenset(
    {
        "default",
        "idle",
        "idle_blink",
        "walk",
        "rest",
        "swim",
        "dance",
        "wave",
        "point",
        "stand",
        "cute",
        "pushup",
        "freaky",
        "bow",
        "worm",
        "shake",
        "shrug",
        "dead",
        "crab",
        "happy",
        "talk_happy",
        "sad",
        "talk_sad",
        "angry",
        "talk_angry",
        "surprised",
        "talk_surprised",
        "sleepy",
        "talk_sleepy",
        "love",
        "talk_love",
        "excited",
        "talk_excited",
        "confused",
        "talk_confused",
        "thinking",
        "talk_thinking",
    }
)


class SandboxReaper(Protocol):
    async def remove(self, session_key: str) -> None: ...


class ServiceAdvertiser(Protocol):
    def start(self) -> None: ...

    def refresh(self) -> bool: ...

    def stop(self) -> None: ...


async def cleanup_expired_conversation_sandboxes(
    *,
    registry: ConversationRegistry,
    reaper: SandboxReaper,
    agent_id: str,
    session_key_secret: str,
) -> None:
    for conversation in registry.prune_expired():
        session_key = build_openclaw_session_key(
            agent_id=agent_id,
            conversation_id=conversation.conversation_id,
            session_key_secret=session_key_secret,
        )
        try:
            await reaper.remove(session_key)
        except (OSError, RuntimeError):
            logger.exception("could not remove expired OpenClaw sandbox")


async def _run_sandbox_cleanup_loop(
    *,
    registry: ConversationRegistry,
    reaper: SandboxReaper,
    agent_id: str,
    session_key_secret: str,
    interval_seconds: int,
) -> None:
    while True:
        await asyncio.sleep(interval_seconds)
        await cleanup_expired_conversation_sandboxes(
            registry=registry,
            reaper=reaper,
            agent_id=agent_id,
            session_key_secret=session_key_secret,
        )


async def _run_mdns_refresh_loop(
    *,
    advertiser: ServiceAdvertiser,
    interval_seconds: int,
) -> None:
    while True:
        await asyncio.sleep(interval_seconds)
        try:
            changed = await asyncio.to_thread(advertiser.refresh)
        except RuntimeError:
            # Retain the previous advertisement and retry after the next
            # interval; networking may still be recovering from DHCP/Wi-Fi.
            logger.warning("could not refresh the Gateway mDNS advertisement")
            continue
        if changed:
            logger.info("refreshed Gateway mDNS advertisement after LAN address change")


@dataclass(slots=True)
class DeviceSession:
    device_id: str
    user_id: str
    session_id: str
    conversation_id: str
    outgoing_sequence: int = 0
    expected_incoming_sequence: int = 1
    is_listening: bool = False
    turn_id: str | None = None
    opus_packets: list[bytes] = field(default_factory=list)
    last_audio_sequence: int = -1
    uplink_generation_id: int | None = None
    uplink_packet_count: int = 0
    uplink_audio_bytes: int = 0
    active_turn_task: asyncio.Task[None] | None = None
    active_generation: int = 0
    turn_epoch: int = 0
    send_lock: asyncio.Lock = field(default_factory=asyncio.Lock)


class RemoteControlRequest(BaseModel):
    """A local operator command that never passes through OpenClaw."""

    model_config = ConfigDict(extra="forbid", frozen=True)

    kind: Literal["action", "expression", "servo", "settings", "stop"]
    action: str | None = None
    expression: str | None = None
    servo: int | None = None
    angle: int | None = None
    frame_delay_ms: int | None = None
    walk_cycles: int | None = None
    motor_current_delay_ms: int | None = None

    @model_validator(mode="after")
    def validate_shape(self) -> RemoteControlRequest:
        if self.kind == "action":
            if (
                self.action not in REMOTE_ACTIONS
                or self.expression is not None
                or self.servo is not None
                or self.angle is not None
                or self.frame_delay_ms is not None
                or self.walk_cycles is not None
                or self.motor_current_delay_ms is not None
            ):
                raise ValueError("action command must contain one supported action")
        elif self.kind == "expression":
            if (
                self.expression not in REMOTE_EXPRESSIONS
                or self.action is not None
                or self.servo is not None
                or self.angle is not None
                or self.frame_delay_ms is not None
                or self.walk_cycles is not None
                or self.motor_current_delay_ms is not None
            ):
                raise ValueError("expression command must contain one supported expression")
        elif self.kind == "servo":
            if (
                self.action is not None
                or self.expression is not None
                or self.servo is None
                or self.angle is None
                or self.frame_delay_ms is not None
                or self.walk_cycles is not None
                or self.motor_current_delay_ms is not None
                or not 1 <= self.servo <= 8
                or not 0 <= self.angle <= 180
            ):
                raise ValueError("servo command must contain servo 1-8 and angle 0-180")
        elif self.kind == "settings":
            if (
                self.action is not None
                or self.expression is not None
                or self.servo is not None
                or self.angle is not None
                or self.frame_delay_ms is None
                or self.walk_cycles is None
                or self.motor_current_delay_ms is None
                or not 10 <= self.frame_delay_ms <= 1_000
                or not 1 <= self.walk_cycles <= 50
                or not 0 <= self.motor_current_delay_ms <= 500
            ):
                raise ValueError("settings command contains values outside the safe range")
        elif (
            self.action is not None
            or self.expression is not None
            or self.servo is not None
            or self.angle is not None
            or self.frame_delay_ms is not None
            or self.walk_cycles is not None
            or self.motor_current_delay_ms is not None
        ):
            raise ValueError("stop command cannot contain parameters")
        return self


class ActiveDeviceSessionRegistry:
    """Keeps exactly one authenticated WebSocket owner per device ID."""

    def __init__(self) -> None:
        self._session_ids: dict[str, str] = {}
        self._lock = asyncio.Lock()

    async def claim(self, device_id: str, session_id: str) -> bool:
        async with self._lock:
            if device_id in self._session_ids:
                return False
            self._session_ids[device_id] = session_id
            return True

    async def release(self, device_id: str, session_id: str) -> bool:
        async with self._lock:
            if self._session_ids.get(device_id) != session_id:
                return False
            del self._session_ids[device_id]
            return True


class DeviceControlRegistry:
    """Routes local-dashboard controls over the ESP32's authenticated WSS link."""

    def __init__(self) -> None:
        self._connections: dict[str, tuple[WebSocket, DeviceSession]] = {}
        self._lock = asyncio.Lock()

    async def register(self, websocket: WebSocket, session: DeviceSession) -> None:
        async with self._lock:
            self._connections[session.device_id] = (websocket, session)

    async def unregister(self, session: DeviceSession) -> None:
        async with self._lock:
            current = self._connections.get(session.device_id)
            if current is not None and current[1].session_id == session.session_id:
                del self._connections[session.device_id]

    async def dispatch(self, device_id: str, command: RemoteControlRequest) -> None:
        async with self._lock:
            connection = self._connections.get(device_id)
        if connection is None:
            raise RuntimeError("device is offline")
        websocket, session = connection
        if command.kind != "stop" and (
            session.is_listening
            or session.active_generation != 0
            or (session.active_turn_task is not None and not session.active_turn_task.done())
        ):
            raise RuntimeError("device is busy with a voice turn")
        payload = command.model_dump(exclude_none=True)
        try:
            await _send_control(
                websocket,
                session,
                "operator.control",
                turn_id=None,
                request_id=f"ctl_{uuid.uuid4().hex}",
                payload=payload,
            )
        except Exception as exc:
            await self.unregister(session)
            raise RuntimeError("device control delivery failed") from exc


def _build_pipeline(
    settings: Settings, *, observer: ObservabilityStore | None = None
) -> ConversationPipeline:
    audio_format = AudioFormat()
    asr: AsrProvider
    agent: AgentProvider
    tts: TtsProvider
    web_search: WebSearchProvider | None = None

    dashscope_client: DashScopeAudioClient | None = None
    if settings.asr_provider == "dashscope" or settings.tts_provider == "dashscope":
        require_remote_speech_consent(settings.allow_remote_speech)
        if settings.dashscope_api_key is None:
            raise ValueError("DashScope API key is required")
        dashscope_client = DashScopeAudioClient(
            api_key=settings.dashscope_api_key.get_secret_value(),
            http_base_url=settings.dashscope_http_base_url,
            websocket_base_url=settings.dashscope_websocket_base_url,
        )

    if dashscope_client is None:
        raise ValueError("DashScope audio client is not configured")
    asr = DashScopeAsrProvider(
        client=dashscope_client,
        model=settings.dashscope_asr_model,
        language=settings.dashscope_asr_language,
        timeout_seconds=settings.dashscope_timeout_seconds,
    )

    if settings.openclaw_token is None or settings.openclaw_session_key_secret is None:
        raise ValueError("OpenClaw token and session key secret are required")
    agent = OpenClawAgentProvider(
        url=settings.openclaw_url,
        token=settings.openclaw_token.get_secret_value(),
        agent_id=settings.openclaw_agent_id,
        session_key_secret=settings.openclaw_session_key_secret.get_secret_value(),
        timeout_seconds=settings.openclaw_timeout_seconds,
        max_attempts=settings.openclaw_max_attempts,
        retry_initial_delay_seconds=settings.openclaw_retry_initial_delay_seconds,
        retry_max_delay_seconds=settings.openclaw_retry_max_delay_seconds,
        abort_timeout_seconds=settings.openclaw_abort_timeout_seconds,
    )

    tts = DashScopeTtsProvider(
        client=dashscope_client,
        default_voice_id=settings.dashscope_tts_voice_id,
        model=settings.dashscope_tts_model,
        timeout_seconds=settings.dashscope_timeout_seconds,
        audio_format=audio_format,
    )

    if settings.web_search_enabled:
        if settings.dashscope_api_key is None:
            raise ValueError("DashScope API key is required for web search")
        web_search = DashScopeWebSearchProvider(
            api_key=settings.dashscope_api_key.get_secret_value(),
            http_base_url=settings.dashscope_http_base_url,
            model=settings.web_search_model,
            timeout_seconds=settings.web_search_timeout_seconds,
        )

    recording_store = create_test_recording_store(
        enabled=settings.save_test_recordings,
        output_directory=settings.test_recording_dir,
        limit=settings.test_recording_limit,
    )

    return ConversationPipeline(
        codec_factory=lambda: OpusCodec(audio_format),
        asr=asr,
        agent=agent,
        tts=tts,
        audio_format=audio_format,
        observer=observer,
        web_search=web_search,
        recording_store=recording_store,
    )


def _provider_status(settings: Settings) -> dict[str, str]:
    return {
        "asr": settings.asr_provider,
        "agent": settings.provider_mode,
        "tts": settings.tts_provider,
        "web_search": settings.web_search_model if settings.web_search_enabled else "disabled",
        "opus": "libopus",
    }


def create_app(
    settings: Settings | None = None,
    *,
    pipeline: ConversationPipeline | None = None,
    sandbox_reaper: SandboxReaper | None = None,
    mdns_advertiser: ServiceAdvertiser | None = None,
    observability: ObservabilityStore | None = None,
    device_controls: DeviceControlRegistry | None = None,
) -> FastAPI:
    resolved_settings = settings or get_settings()
    resolved_observability = observability or ObservabilityStore(
        max_events=resolved_settings.dashboard_event_limit,
        expose_debug_content=resolved_settings.dashboard_debug_content,
    )
    resolved_pipeline = pipeline or _build_pipeline(
        resolved_settings, observer=resolved_observability
    )
    advertiser = mdns_advertiser or MdnsAdvertiser(resolved_settings)
    conversations = ConversationRegistry(ttl_seconds=resolved_settings.conversation_ttl_seconds)
    active_device_sessions = ActiveDeviceSessionRegistry()
    resolved_device_controls = device_controls or DeviceControlRegistry()
    resolved_reaper = sandbox_reaper
    if resolved_reaper is None and resolved_settings.openclaw_sandbox_cleanup_enabled:
        resolved_reaper = OpenClawSandboxReaper()

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        app.state.pipeline = resolved_pipeline
        cleanup_task: asyncio.Task[None] | None = None
        mdns_refresh_task: asyncio.Task[None] | None = None
        serial_monitor_task: asyncio.Task[None] | None = None
        if resolved_reaper is not None:
            if resolved_settings.openclaw_session_key_secret is None:
                raise ValueError("OpenClaw session key secret is required for sandbox cleanup")
            cleanup_task = asyncio.create_task(
                _run_sandbox_cleanup_loop(
                    registry=conversations,
                    reaper=resolved_reaper,
                    agent_id=resolved_settings.openclaw_agent_id,
                    session_key_secret=resolved_settings.openclaw_session_key_secret.get_secret_value(),
                    interval_seconds=resolved_settings.conversation_cleanup_interval_seconds,
                )
            )
        if resolved_settings.enable_mdns:
            await asyncio.to_thread(advertiser.start)
            mdns_refresh_task = asyncio.create_task(
                _run_mdns_refresh_loop(
                    advertiser=advertiser,
                    interval_seconds=resolved_settings.mdns_refresh_interval_seconds,
                )
            )
        if (
            resolved_settings.serial_monitor_enabled
            and resolved_settings.serial_monitor_device_id is not None
        ):
            serial_monitor = FirmwareSerialMonitor(
                port=resolved_settings.serial_monitor_port,
                baud_rate=resolved_settings.serial_monitor_baud_rate,
                device_id=resolved_settings.serial_monitor_device_id,
                store=resolved_observability,
            )
            serial_monitor_task = asyncio.create_task(
                serial_monitor.run_forever(), name="sesame-firmware-serial-monitor"
            )
        try:
            yield
        finally:
            if serial_monitor_task is not None:
                serial_monitor_task.cancel()
                try:
                    await serial_monitor_task
                except asyncio.CancelledError:
                    pass
            if mdns_refresh_task is not None:
                mdns_refresh_task.cancel()
                try:
                    await mdns_refresh_task
                except asyncio.CancelledError:
                    pass
            if resolved_settings.enable_mdns:
                await asyncio.to_thread(advertiser.stop)
            if cleanup_task is not None:
                cleanup_task.cancel()
                try:
                    await cleanup_task
                except asyncio.CancelledError:
                    pass

    app = FastAPI(title="Sesame Voice Gateway", version="0.1.0", lifespan=lifespan)
    app.state.observability = resolved_observability
    app.state.device_controls = resolved_device_controls

    @app.get("/healthz")
    async def health() -> dict[str, Any]:
        return {
            "status": "ok",
            "gateway_id": resolved_settings.gateway_id,
            "providers": _provider_status(resolved_settings),
        }

    @app.get("/dashboard", response_class=HTMLResponse, include_in_schema=False)
    async def dashboard(request: Request) -> HTMLResponse:
        _require_loopback_dashboard_access(request)
        html = files("sesame_voice_gateway").joinpath("dashboard.html").read_text(
            encoding="utf-8"
        )
        return HTMLResponse(html, headers={"Cache-Control": "no-store"})

    @app.get("/console", response_class=HTMLResponse, include_in_schema=False)
    async def console(request: Request) -> HTMLResponse:
        _require_loopback_dashboard_access(request)
        html = files("sesame_voice_gateway").joinpath("console.html").read_text(
            encoding="utf-8"
        )
        return HTMLResponse(html, headers={"Cache-Control": "no-store"})

    @app.post("/api/local-control/{device_id}", status_code=202, include_in_schema=False)
    async def local_control(
        device_id: str, command: RemoteControlRequest, request: Request
    ) -> JSONResponse:
        _require_loopback_dashboard_access(request)
        try:
            await resolved_device_controls.dispatch(device_id, command)
        except RuntimeError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc
        resolved_observability.record_device_stage(
            device_id=device_id,
            stage="operator.control",
            status="delivered",
            details={
                "kind": command.kind,
                "action": command.action,
                "expression": command.expression,
            },
            update_current_stage=False,
        )
        return JSONResponse(
            {"status": "accepted", "device_id": device_id, "kind": command.kind},
            status_code=202,
            headers={"Cache-Control": "no-store"},
        )

    @app.get("/api/observability/snapshot", include_in_schema=False)
    async def observability_snapshot(request: Request) -> JSONResponse:
        _require_loopback_dashboard_access(request)
        return JSONResponse(resolved_observability.snapshot(), headers={"Cache-Control": "no-store"})

    @app.get("/api/observability/stream", include_in_schema=False)
    async def observability_stream(request: Request) -> StreamingResponse:
        _require_loopback_dashboard_access(request)
        return StreamingResponse(
            _stream_observability_events(request, resolved_observability),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-store", "X-Accel-Buffering": "no"},
        )

    @app.websocket("/v1/device-stream")
    async def device_stream(websocket: WebSocket) -> None:
        await _handle_device_stream(
            websocket=websocket,
            settings=resolved_settings,
            pipeline=resolved_pipeline,
            conversations=conversations,
            active_device_sessions=active_device_sessions,
            device_controls=resolved_device_controls,
            observability=resolved_observability,
        )

    return app


async def _handle_device_stream(
    *,
    websocket: WebSocket,
    settings: Settings,
    pipeline: ConversationPipeline,
    conversations: ConversationRegistry,
    active_device_sessions: ActiveDeviceSessionRegistry,
    device_controls: DeviceControlRegistry,
    observability: ObservabilityStore,
) -> None:
    await websocket.accept()
    session: DeviceSession | None = None
    try:
        session = await _authenticate_session(
            websocket,
            settings,
            conversations,
            active_device_sessions,
        )
        await device_controls.register(websocket, session)
        observability.device_connected(device_id=session.device_id, session_id=session.session_id)
        await _send_control(
            websocket,
            session,
            "session.ready",
            turn_id=None,
            request_id=None,
            payload={
                "gateway_id": settings.gateway_id,
                "conversation_id": session.conversation_id,
                "protocol_version": 1,
                "audio": _audio_payload(),
            },
        )
        logger.info(
            "session_ready_sent device_id=%s session_id=%s conversation_id=%s",
            session.device_id,
            session.session_id,
            session.conversation_id,
        )
        observability.session_ready(device_id=session.device_id, session_id=session.session_id)
        await _receive_device_messages(websocket, session, pipeline, observability)
    except WebSocketDisconnect:
        return
    except (ControlProtocolError, AudioProtocolError, ValueError) as exc:
        # Validation errors can include untrusted input. Do not mirror or log it.
        logger.info("closing invalid device session error_type=%s", type(exc).__name__)
        await websocket.close(code=4400, reason="invalid protocol frame")
    finally:
        if session is not None:
            _cancel_active_turn(session)
            observability.device_disconnected(
                device_id=session.device_id, session_id=session.session_id
            )
            await active_device_sessions.release(session.device_id, session.session_id)
            await device_controls.unregister(session)


async def _authenticate_session(
    websocket: WebSocket,
    settings: Settings,
    conversations: ConversationRegistry,
    active_device_sessions: ActiveDeviceSessionRegistry,
) -> DeviceSession:
    message = await websocket.receive()
    raw_text = message.get("text")
    if raw_text is None:
        raise ControlProtocolError("first frame must be session.hello JSON")
    event = parse_control_event(raw_text)
    if event.type != "session.hello":
        raise ControlProtocolError("first control event must be session.hello")
    if event.sequence != 0:
        raise ControlProtocolError("session.hello must use control sequence 0")
    if event.session_id is not None or event.turn_id is not None or event.request_id is not None:
        raise ControlProtocolError("session.hello must not carry an active session or turn")

    device_id = event.payload["device_id"]
    if event.payload["gateway_id"] != settings.gateway_id:
        await websocket.close(code=4404, reason="gateway_id mismatch")
        raise WebSocketDisconnect(code=4404)

    expected_token = settings.device_tokens.get(device_id)
    supplied_token = _bearer_token(websocket.headers.get("authorization"))
    if (
        expected_token is None
        or supplied_token is None
        or not secrets.compare_digest(expected_token, supplied_token)
    ):
        await websocket.close(code=4401, reason="invalid device credential")
        raise WebSocketDisconnect(code=4401)

    user_id = settings.device_users.get(device_id)
    if not user_id:
        await websocket.close(code=4403, reason="device has no user mapping")
        raise WebSocketDisconnect(code=4403)

    session = DeviceSession(
        device_id=device_id,
        user_id=user_id,
        session_id=f"ses_{uuid.uuid4().hex}",
        conversation_id="",
    )
    if not await active_device_sessions.claim(device_id, session.session_id):
        await websocket.close(code=4409, reason="device already has an active session")
        raise WebSocketDisconnect(code=4409)

    try:
        conversation = conversations.resolve_or_reissue_for_authenticated_device(
            requested_conversation_id=event.payload["conversation_id"],
            device_id=device_id,
            user_id=user_id,
        )
    except Exception:
        await active_device_sessions.release(device_id, session.session_id)
        raise
    session.conversation_id = conversation.conversation_id

    logger.info(
        "device_session_authenticated device_id=%s session_id=%s conversation_id=%s",
        session.device_id,
        session.session_id,
        session.conversation_id,
    )
    return session


def _bearer_token(header: str | None) -> str | None:
    if header is None:
        return None
    scheme, separator, token = header.partition(" ")
    if separator != " " or scheme.lower() != "bearer" or not token:
        return None
    return token


def _require_loopback_dashboard_access(request: Request) -> None:
    client = request.client
    if client is None:
        raise HTTPException(status_code=403, detail="dashboard requires a local browser")
    try:
        client_address = str(ip_address(client.host))
    except ValueError:
        raise HTTPException(status_code=403, detail="dashboard requires a local browser")
    if client_address not in _local_interface_addresses():
        raise HTTPException(status_code=403, detail="dashboard is available only on this computer")


def _local_interface_addresses() -> set[str]:
    """Return IP addresses owned by this computer, including its active LAN IP.

    A browser opening `https://sesame-gateway.local` reaches the computer through
    its LAN address rather than 127.0.0.1. Checking only loopback would therefore
    reject the owner while not adding meaningful protection.
    """
    addresses = {"127.0.0.1", "::1"}
    hostnames = {socket.gethostname(), socket.getfqdn(), "localhost"}
    for hostname in hostnames:
        try:
            results = socket.getaddrinfo(hostname, None, type=socket.SOCK_STREAM)
        except OSError:
            continue
        for result in results:
            try:
                addresses.add(str(ip_address(result[4][0])))
            except ValueError:
                continue

    # UDP connect selects the active IPv4 interface without sending a packet.
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("192.0.2.1", 9))
            addresses.add(str(ip_address(probe.getsockname()[0])))
    except OSError:
        pass
    return addresses


async def _stream_observability_events(
    request: Request, store: ObservabilityStore
) -> AsyncIterator[str]:
    queue = store.subscribe()
    try:
        snapshot = json.dumps(store.snapshot(), ensure_ascii=False, separators=(",", ":"))
        yield f"event: snapshot\ndata: {snapshot}\n\n"
        while not await request.is_disconnected():
            try:
                event = await asyncio.wait_for(queue.get(), timeout=15)
            except TimeoutError:
                yield ": keep-alive\n\n"
                continue
            encoded_event = json.dumps(event, ensure_ascii=False, separators=(",", ":"))
            yield f"event: trace\ndata: {encoded_event}\n\n"
    finally:
        store.unsubscribe(queue)


async def _receive_device_messages(
    websocket: WebSocket,
    session: DeviceSession,
    pipeline: ConversationPipeline,
    observability: ObservabilityStore,
) -> None:
    while True:
        message = await websocket.receive()
        if message["type"] == "websocket.disconnect":
            raise WebSocketDisconnect(code=message.get("code", 1000))
        if message.get("bytes") is not None:
            _receive_audio(session, message["bytes"], observability)
            continue
        raw_text = message.get("text")
        if raw_text is None:
            raise ControlProtocolError("WebSocket frame must contain JSON text or binary audio")
        event = parse_control_event(raw_text)
        await _handle_control_event(websocket, session, pipeline, event, observability)


def _receive_audio(
    session: DeviceSession, message: bytes, observability: ObservabilityStore | None = None
) -> None:
    if not session.is_listening:
        raise AudioProtocolError("audio received outside an active listening turn")
    frame = unpack_audio_frame(message)
    if frame.direction is not AudioDirection.UPLINK:
        raise AudioProtocolError("device audio must use uplink direction")
    if frame.stream_id != 1:
        raise AudioProtocolError("uplink audio must use stream_id 1")
    if frame.flags != 0 or frame.encoding is not AudioEncoding.OPUS:
        raise AudioProtocolError("production uplink requires Opus flags=0")
    if frame.generation_id == 0:
        raise AudioProtocolError("uplink audio generation_id must be non-zero")
    if frame.sequence != session.last_audio_sequence + 1:
        raise AudioProtocolError("audio sequence is not contiguous")
    if (
        session.uplink_generation_id is not None
        and frame.generation_id != session.uplink_generation_id
    ):
        raise AudioProtocolError("uplink audio generation must not change within a turn")
    if session.uplink_packet_count >= MAX_UPLINK_PACKETS:
        raise AudioProtocolError("uplink audio exceeds the 30-second duration limit")
    if session.uplink_audio_bytes + len(frame.payload) > MAX_UPLINK_OPUS_BYTES:
        raise AudioProtocolError("uplink audio exceeds the 30-second byte limit")

    session.last_audio_sequence = frame.sequence
    session.uplink_generation_id = frame.generation_id
    session.uplink_packet_count += 1
    session.uplink_audio_bytes += len(frame.payload)
    session.opus_packets.append(frame.payload)
    if observability is not None and (
        session.uplink_packet_count == 1 or session.uplink_packet_count % 10 == 0
    ):
        observability.record_audio_progress(
            device_id=session.device_id,
            turn_id=session.turn_id or "unknown_turn",
            direction="up",
            packet_count=session.uplink_packet_count,
            byte_count=session.uplink_audio_bytes,
        )


def _clear_captured_audio(session: DeviceSession) -> None:
    session.opus_packets.clear()
    session.last_audio_sequence = -1
    session.uplink_generation_id = None
    session.uplink_packet_count = 0
    session.uplink_audio_bytes = 0


def _cancel_active_turn(session: DeviceSession) -> bool:
    """Invalidate an old turn before it can send a plan, audio, or action."""
    session.turn_epoch += 1
    task = session.active_turn_task
    session.active_turn_task = None
    session.active_generation = 0
    if task is None or task.done():
        return False
    task.cancel()
    return True


def _turn_is_current(session: DeviceSession, turn_epoch: int) -> bool:
    return (
        session.turn_epoch == turn_epoch
        and session.active_turn_task is asyncio.current_task()
    )


async def _run_turn(
    *,
    websocket: WebSocket,
    session: DeviceSession,
    pipeline: ConversationPipeline,
    context: ConversationContext,
    opus_packets: list[bytes],
    turn_epoch: int,
    observability: ObservabilityStore | None = None,
) -> None:
    try:
        result = await pipeline.process_turn(context=context, opus_packets=opus_packets)
    except asyncio.CancelledError:
        logger.info("voice turn cancelled before a response was sent")
        raise
    except PolicyViolation:
        if observability is not None:
            observability.record_stage(
                device_id=session.device_id,
                turn_id=context.turn_id,
                stage="response.plan",
                status="failed",
                details={"error_code": "agent_output_rejected"},
            )
        if _turn_is_current(session, turn_epoch):
            await _send_control(
                websocket,
                session,
                "error",
                turn_id=context.turn_id,
                request_id=None,
                payload={
                    "code": "agent_output_rejected",
                    "message": "agent response did not pass the device safety policy",
                    "retryable": False,
                },
            )
    except OpenClawUnavailableError:
        logger.warning("OpenClaw was unavailable before a safe response was produced")
        if observability is not None:
            observability.record_stage(
                device_id=session.device_id,
                turn_id=context.turn_id,
                stage="openclaw",
                status="failed",
                details={"error_code": "openclaw_unavailable"},
            )
        if _turn_is_current(session, turn_epoch):
            await _send_control(
                websocket,
                session,
                "error",
                turn_id=context.turn_id,
                request_id=None,
                payload={
                    "code": "openclaw_unavailable",
                    "message": "the local assistant is temporarily unavailable",
                    "retryable": True,
                },
            )
    except Exception as exc:
        # Provider failures may contain user content. Keep both logs and the
        # device response metadata-only and generic.
        logger.warning(
            "voice_pipeline_failed device_id=%s session_id=%s error_type=%s",
            session.device_id,
            session.session_id,
            type(exc).__name__,
        )
        if observability is not None:
            observability.record_stage(
                device_id=session.device_id,
                turn_id=context.turn_id,
                stage="pipeline",
                status="failed",
                details={"error_type": type(exc).__name__},
            )
        if _turn_is_current(session, turn_epoch):
            await _send_control(
                websocket,
                session,
                "error",
                turn_id=context.turn_id,
                request_id=None,
                payload={
                    "code": "voice_pipeline_failed",
                    "message": "voice processing failed before a safe response was produced",
                    "retryable": True,
                },
            )
    else:
        if _turn_is_current(session, turn_epoch):
            await _send_turn_result(
                websocket,
                session,
                result,
                turn_id=context.turn_id,
                turn_epoch=turn_epoch,
                observability=observability,
            )
    finally:
        if session.active_turn_task is asyncio.current_task():
            session.active_turn_task = None


def _validate_incoming_control_sequence(
    session: DeviceSession, event: ControlEvent
) -> None:
    if event.sequence != session.expected_incoming_sequence:
        raise ControlProtocolError(
            "device control sequence is not the next expected value"
        )
    if session.expected_incoming_sequence == 0xFFFFFFFF:
        raise ControlProtocolError("device control sequence space is exhausted")
    session.expected_incoming_sequence += 1


async def _handle_control_event(
    websocket: WebSocket,
    session: DeviceSession,
    pipeline: ConversationPipeline,
    event: ControlEvent,
    observability: ObservabilityStore | None = None,
) -> None:
    if event.session_id != session.session_id:
        raise ControlProtocolError("session_id does not match active connection")
    _validate_incoming_control_sequence(session, event)

    if event.type == "listen.start":
        if session.is_listening:
            raise ControlProtocolError("listen.start received while already listening")
        if event.turn_id is None:
            raise ControlProtocolError("listen.start requires turn_id")
        if _cancel_active_turn(session):
            await _send_control(
                websocket,
                session,
                "tts.flush",
                turn_id=None,
                request_id=None,
                payload={"generation_id": 0},
            )
        session.is_listening = True
        session.turn_id = event.turn_id
        _clear_captured_audio(session)
        if observability is not None:
            observability.record_stage(
                device_id=session.device_id,
                turn_id=event.turn_id,
                stage="listen",
                status="started",
                details={"trigger": "device_button"},
            )
        logger.info(
            "voice_listen_started device_id=%s session_id=%s turn_id=%s",
            session.device_id,
            session.session_id,
            session.turn_id,
        )
        return

    if event.type == "listen.stop":
        if not session.is_listening or event.turn_id != session.turn_id:
            raise ControlProtocolError("listen.stop does not match active turn")
        session.is_listening = False
        turn_id = session.turn_id
        context = ConversationContext(
            device_id=session.device_id,
            user_id=session.user_id,
            conversation_id=session.conversation_id,
            turn_id=turn_id or "",
        )
        turn_epoch = session.turn_epoch + 1
        session.turn_epoch = turn_epoch
        opus_packets = list(session.opus_packets)
        uplink_generation_id = session.uplink_generation_id
        uplink_packet_count = session.uplink_packet_count
        uplink_audio_bytes = session.uplink_audio_bytes
        _clear_captured_audio(session)
        session.turn_id = None
        if observability is not None:
            observability.record_audio_progress(
                device_id=session.device_id,
                turn_id=turn_id or "unknown_turn",
                direction="up",
                packet_count=uplink_packet_count,
                byte_count=uplink_audio_bytes,
            )
            observability.record_stage(
                device_id=session.device_id,
                turn_id=turn_id or "unknown_turn",
                stage="listen",
                status="completed",
                details={"generation_id": uplink_generation_id},
            )
        logger.info(
            "voice_turn_captured device_id=%s session_id=%s turn_id=%s packets=%d opus_bytes=%d generation_id=%s",
            session.device_id,
            session.session_id,
            turn_id,
            uplink_packet_count,
            uplink_audio_bytes,
            uplink_generation_id,
        )
        session.active_turn_task = asyncio.create_task(
            _run_turn(
                websocket=websocket,
                session=session,
                pipeline=pipeline,
                context=context,
                opus_packets=opus_packets,
                turn_epoch=turn_epoch,
                observability=observability,
            ),
            name=f"sesame-turn-{turn_id}",
        )
        return

    if event.type == "interrupt":
        session.is_listening = False
        generation_id = session.active_generation
        _cancel_active_turn(session)
        _clear_captured_audio(session)
        session.turn_id = None
        if observability is not None and event.turn_id is not None:
            observability.record_stage(
                device_id=session.device_id,
                turn_id=event.turn_id,
                stage="turn",
                status="cancelled",
                details={"generation_id": generation_id},
            )
        logger.info(
            "voice_turn_interrupted device_id=%s session_id=%s generation_id=%d",
            session.device_id,
            session.session_id,
            generation_id,
        )
        await _send_control(
            websocket,
            session,
            "tts.flush",
            turn_id=event.turn_id,
            request_id=None,
            payload={"generation_id": generation_id},
        )
        return

    if event.type == "action.result":
        logger.info(
            "action_result device_id=%s session_id=%s turn_id=%s request_id=%s status=%s error_code_present=%s",
            session.device_id,
            session.session_id,
            event.turn_id,
            event.request_id,
            event.payload["status"],
            event.payload["error_code"] is not None,
        )
        if observability is not None:
            details = {
                "request_id": event.request_id,
                "error_code": event.payload["error_code"],
            }
            if event.turn_id is None:
                observability.record_device_stage(
                    device_id=session.device_id,
                    stage="operator.control",
                    status=str(event.payload["status"]),
                    details=details,
                    update_current_stage=False,
                )
            else:
                observability.record_stage(
                    device_id=session.device_id,
                    turn_id=event.turn_id,
                    stage="action.execute",
                    status=str(event.payload["status"]),
                    details=details,
                )
        return

    raise ControlProtocolError(f"event is not allowed from device: {event.type}")


async def _send_turn_result(
    websocket: WebSocket,
    session: DeviceSession,
    result: TurnResult,
    *,
    turn_id: str | None = None,
    turn_epoch: int | None = None,
    observability: ObservabilityStore | None = None,
) -> None:
    if turn_epoch is not None and not _turn_is_current(session, turn_epoch):
        return
    turn_id = session.turn_id if turn_id is None else turn_id
    action = result.agent.actions[0] if result.agent.actions else None
    action_request_id = f"act_{uuid.uuid4().hex}" if action is not None else None

    def response_plan_payload(timestamp_ms: int) -> dict[str, Any]:
        return {
            "generation_id": result.generation_id,
            "expression_id": result.agent.expression.name,
            "expression_ttl_ms": result.agent.expression.ttl_ms,
            "action_id": action.name if action is not None else None,
            "action_request_id": action_request_id,
            "action_duration_ms": action.duration_ms if action is not None else None,
            # The action deadline is derived while holding the same send lock
            # as this event's timestamp, so ESP32 sees a usable 5-second span.
            "action_deadline_ms": timestamp_ms + 5_000 if action is not None else None,
        }

    session.active_generation = result.generation_id
    await _send_control(
        websocket,
        session,
        "response.plan",
        turn_id=turn_id,
        request_id=None,
        payload=response_plan_payload,
    )
    if observability is not None and turn_id is not None:
        observability.record_stage(
            device_id=session.device_id,
            turn_id=turn_id,
            stage="response.plan",
            status="completed",
            details={
                "generation_id": result.generation_id,
                "expression": result.agent.expression.name,
                "action": action.name if action is not None else None,
                "voice_style": result.agent.voice.style,
            },
        )
    logger.info(
        "response_plan_sent device_id=%s session_id=%s generation_id=%d expression_id=%s action_id=%s",
        session.device_id,
        session.session_id,
        result.generation_id,
        result.agent.expression.name,
        action.name if action is not None else None,
    )
    await _send_control(
        websocket,
        session,
        "tts.start",
        turn_id=turn_id,
        request_id=None,
        payload={"generation_id": result.generation_id},
    )
    if observability is not None and turn_id is not None:
        observability.record_stage(
            device_id=session.device_id,
            turn_id=turn_id,
            stage="tts.downlink",
            status="started",
            details={"generation_id": result.generation_id},
        )
    logger.info(
        "tts_started device_id=%s session_id=%s generation_id=%d opus_packets=%d",
        session.device_id,
        session.session_id,
        result.generation_id,
        len(result.opus_packets),
    )
    started_at = _timestamp_ms()
    downlink_bytes = 0
    for sequence, packet in enumerate(result.opus_packets):
        if turn_epoch is not None and not _turn_is_current(session, turn_epoch):
            return
        await _send_binary(
            websocket,
            session,
            pack_audio_frame(
                AudioFrame(
                    direction=AudioDirection.DOWNLINK,
                    flags=0,
                    stream_id=2,
                    generation_id=result.generation_id,
                    sequence=sequence,
                    timestamp_ms=started_at + sequence * 20,
                    payload=packet,
                )
            ),
        )
        downlink_bytes += len(packet)
        if observability is not None and turn_id is not None and (
            sequence == 0 or (sequence + 1) % 10 == 0
        ):
            observability.record_audio_progress(
                device_id=session.device_id,
                turn_id=turn_id,
                direction="down",
                packet_count=sequence + 1,
                byte_count=downlink_bytes,
            )
        if sequence + 1 < len(result.opus_packets):
            await asyncio.sleep(0.02)
    if turn_epoch is not None and not _turn_is_current(session, turn_epoch):
        return
    await _send_control(
        websocket,
        session,
        "tts.stop",
        turn_id=turn_id,
        request_id=None,
        payload={"generation_id": result.generation_id, "reason": "completed"},
    )
    if observability is not None and turn_id is not None:
        observability.record_audio_progress(
            device_id=session.device_id,
            turn_id=turn_id,
            direction="down",
            packet_count=len(result.opus_packets),
            byte_count=downlink_bytes,
        )
        observability.record_stage(
            device_id=session.device_id,
            turn_id=turn_id,
            stage="tts.downlink",
            status="completed",
            elapsed_ms=len(result.opus_packets) * 20,
            details={"generation_id": result.generation_id},
        )
    logger.info(
        "tts_completed device_id=%s session_id=%s generation_id=%d opus_packets=%d",
        session.device_id,
        session.session_id,
        result.generation_id,
        len(result.opus_packets),
    )
    if turn_epoch is None or _turn_is_current(session, turn_epoch):
        session.active_generation = 0


async def _send_binary(
    websocket: WebSocket, session: DeviceSession, payload: bytes
) -> None:
    async with session.send_lock:
        await websocket.send_bytes(payload)


async def _send_control(
    websocket: WebSocket,
    session: DeviceSession,
    event_type: Any,
    *,
    turn_id: str | None,
    request_id: str | None,
    payload: dict[str, Any] | Callable[[int], dict[str, Any]],
) -> None:
    async with session.send_lock:
        timestamp_ms = _timestamp_ms()
        resolved_payload = payload(timestamp_ms) if callable(payload) else payload
        event = ControlEvent(
            v=1,
            type=event_type,
            session_id=session.session_id,
            turn_id=turn_id,
            request_id=request_id,
            sequence=session.outgoing_sequence,
            timestamp_ms=timestamp_ms,
            payload=resolved_payload,
        )
        session.outgoing_sequence += 1
        await websocket.send_text(serialize_control_event(event))


def _audio_payload() -> dict[str, Any]:
    return {
        "codec": "opus",
        "sample_rate": 16_000,
        "channels": 1,
        "frame_duration_ms": 20,
    }


def _timestamp_ms() -> int:
    return int(time.time() * 1_000)


app = create_app()
