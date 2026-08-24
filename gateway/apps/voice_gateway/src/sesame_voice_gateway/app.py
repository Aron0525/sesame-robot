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
from pydantic import BaseModel, ConfigDict, StrictBool, model_validator

from sesame_voice_gateway.audio.opus import OpusCodec
from sesame_voice_gateway.config import Settings, get_settings
from sesame_voice_gateway.conversations import ConversationRegistry
from sesame_voice_gateway.discovery import MdnsAdvertiser
from sesame_voice_gateway.local_pcm_test import LocalPcmTestError, LocalPcmTestQueue
from sesame_voice_gateway.openclaw.client import (
    OpenClawAgentProvider,
    OpenClawUnavailableError,
    build_openclaw_session_key,
)
from sesame_voice_gateway.openclaw.sse_bridge import load_openclaw_gateway_token
from sesame_voice_gateway.openclaw.sse_client import OpenClawSseAgentProvider
from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.pipeline import (
    MAX_UPLINK_PACKETS,
    ConversationContext,
    ConversationPipeline,
    SilentDiscard,
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
from sesame_voice_gateway.providers.base import (
    AgentProvider,
    AgentResult,
    AsrProvider,
    AsrResult,
    AudioFormat,
    TtsProvider,
)
from sesame_voice_gateway.providers.dashscope import (
    DashScopeAsrProvider,
    DashScopeAudioClient,
    DashScopeTtsProvider,
)
from sesame_voice_gateway.recordings import create_test_recording_store
from sesame_voice_gateway.sandbox_cleanup import OpenClawSandboxReaper
from sesame_voice_gateway.serial_monitor import FirmwareSerialMonitor
from sesame_voice_gateway.streaming import (
    StreamAudio,
    StreamFinished,
    StreamOutput,
    StreamStart,
    StreamingConversationPipeline,
)
from sesame_voice_gateway.tools.web_search import DashScopeWebSearchProvider, WebSearchProvider

logger = logging.getLogger(__name__)

VoicePipeline = ConversationPipeline | StreamingConversationPipeline

MAX_UPLINK_OPUS_BYTES = MAX_UPLINK_PACKETS * MAX_OPUS_PACKET_BYTES
OPERATOR_CONTROL_ACK_TIMEOUT_SECONDS = 3.0
REMOTE_ACTIONS = frozenset(
    {
        "rest",
        "stand",
        "wave",
        "dance",
        "swim",
        "pushup",
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
    capture_trigger: str = "unknown"
    opus_packets: list[bytes] = field(default_factory=list)
    last_audio_sequence: int = -1
    uplink_generation_id: int | None = None
    uplink_packet_count: int = 0
    uplink_audio_bytes: int = 0
    active_turn_task: asyncio.Task[None] | None = None
    active_generation: int = 0
    turn_epoch: int = 0
    send_lock: asyncio.Lock = field(default_factory=asyncio.Lock)
    # The ESP32 reports this optional, authenticated control-plane telemetry
    # while a downlink is active. Audio remains WSS binary Opus.
    playback_stats: dict[str, Any] | None = None
    playback_stats_generation: int = 0
    playback_stats_turn_id: str | None = None
    playback_stats_revision: int = 0
    playback_stats_event: asyncio.Event = field(default_factory=asyncio.Event)
    # Local pause/resume is deliberately separate from telemetry: the
    # Gateway must stop producing packets immediately after it accepts a
    # pause request, rather than waiting for a round trip to ESP32.
    playback_paused: bool = False
    playback_pause_event: asyncio.Event = field(default_factory=asyncio.Event)


class RemoteControlRequest(BaseModel):
    """A local operator command that never passes through OpenClaw."""

    model_config = ConfigDict(extra="forbid", frozen=True)

    kind: Literal[
        "action",
        "expression",
        "servo",
        "settings",
        "wakeword_settings",
        "speaker_verification_settings",
        "stop",
    ]
    action: str | None = None
    expression: str | None = None
    servo: int | None = None
    angle: int | None = None
    frame_delay_ms: int | None = None
    walk_cycles: int | None = None
    motor_current_delay_ms: int | None = None
    wake_threshold_hundredths: int | None = None
    speaker_verification_enabled: StrictBool | None = None

    @model_validator(mode="after")
    def validate_shape(self) -> RemoteControlRequest:
        if self.kind == "action":
            if self.action not in REMOTE_ACTIONS or any(
                value is not None
                for value in (
                    self.expression,
                    self.servo,
                    self.angle,
                    self.frame_delay_ms,
                    self.walk_cycles,
                    self.motor_current_delay_ms,
                    self.speaker_verification_enabled,
                )
            ):
                raise ValueError("action command must contain one supported action")
        elif self.kind == "expression":
            if self.expression not in REMOTE_EXPRESSIONS or any(
                value is not None
                for value in (
                    self.action,
                    self.servo,
                    self.angle,
                    self.frame_delay_ms,
                    self.walk_cycles,
                    self.motor_current_delay_ms,
                    self.speaker_verification_enabled,
                )
            ):
                raise ValueError("expression command must contain one supported expression")
        elif self.kind == "servo":
            if (
                self.action is not None
                or self.expression is not None
                or self.servo is None
                or self.angle is None
                or not 1 <= self.servo <= 8
                or not 0 <= self.angle <= 180
                or self.frame_delay_ms is not None
                or self.walk_cycles is not None
                or self.motor_current_delay_ms is not None
                or self.speaker_verification_enabled is not None
            ):
                raise ValueError("servo command must contain servo 1-8 and angle 0-180")
        elif self.kind == "settings":
            if (
                self.action is not None
                or self.expression is not None
                or self.servo is not None
                or self.angle is not None
                or self.frame_delay_ms is None
                or not 10 <= self.frame_delay_ms <= 1_000
                or self.walk_cycles is None
                or not 1 <= self.walk_cycles <= 50
                or self.motor_current_delay_ms is None
                or not 0 <= self.motor_current_delay_ms <= 500
                or self.speaker_verification_enabled is not None
            ):
                raise ValueError("settings command must contain valid motion settings")
        elif self.kind == "wakeword_settings":
            if (
                self.action is not None or self.expression is not None or
                self.servo is not None or self.angle is not None or
                self.frame_delay_ms is not None or self.walk_cycles is not None or
                self.motor_current_delay_ms is not None or
                self.speaker_verification_enabled is not None or
                self.wake_threshold_hundredths is None or
                not 5 <= self.wake_threshold_hundredths <= 95
            ):
                raise ValueError("wakeword_settings requires threshold 0.05-0.95")
        elif self.kind == "speaker_verification_settings":
            if (
                self.action is not None or self.expression is not None or
                self.servo is not None or self.angle is not None or
                self.frame_delay_ms is not None or self.walk_cycles is not None or
                self.motor_current_delay_ms is not None or
                self.wake_threshold_hundredths is not None or
                self.speaker_verification_enabled is None
            ):
                raise ValueError(
                    "speaker_verification_settings requires a boolean enabled value"
                )
        elif any(
            value is not None
            for value in (
                self.action,
                self.expression,
                self.servo,
                self.angle,
                self.frame_delay_ms,
                self.walk_cycles,
                self.motor_current_delay_ms,
                self.wake_threshold_hundredths,
                self.speaker_verification_enabled,
            )
        ):
            raise ValueError("stop command cannot contain parameters")
        return self

    def payload(self) -> dict[str, Any]:
        return self.model_dump(exclude_none=True)


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


@dataclass(frozen=True)
class OperatorControlAcknowledgement:
    """The ESP32 result bound to one operator-issued control request."""

    request_id: str
    status: str
    error_code: str | None


class DeviceControlRegistry:
    """Routes local-dashboard controls over the ESP32's authenticated WSS link."""

    def __init__(self) -> None:
        self._connections: dict[str, tuple[WebSocket, DeviceSession]] = {}
        self._lock = asyncio.Lock()
        self._test_play_lock = asyncio.Lock()
        self._playback_control_lock = asyncio.Lock()
        self._pending_operator_controls: dict[
            tuple[str, str], asyncio.Future[OperatorControlAcknowledgement]
        ] = {}

    async def register(self, websocket: WebSocket, session: DeviceSession) -> None:
        async with self._lock:
            self._connections[session.device_id] = (websocket, session)

    async def unregister(self, session: DeviceSession) -> None:
        session.playback_paused = False
        session.playback_pause_event.set()
        async with self._lock:
            current = self._connections.get(session.device_id)
            if current is not None and current[1].session_id == session.session_id:
                del self._connections[session.device_id]
            pending_keys = [
                key
                for key in self._pending_operator_controls
                if key[0] == session.session_id
            ]
            for key in pending_keys:
                pending = self._pending_operator_controls.pop(key)
                if not pending.done():
                    pending.set_exception(RuntimeError("ESP32 control connection closed"))

    async def set_playback_paused(
        self, device_id: str, *, paused: bool
    ) -> tuple[str, int]:
        """Pause/resume an authenticated active TTS generation in place."""
        async with self._playback_control_lock:
            async with self._lock:
                connection = self._connections.get(device_id)
            if connection is None:
                raise RuntimeError("device is offline")
            websocket, session = connection
            generation_id = session.active_generation
            turn_id = session.playback_stats_turn_id
            if generation_id == 0 or turn_id is None:
                raise RuntimeError("device has no active audio playback")
            if session.playback_paused == paused:
                return turn_id, generation_id

            # For pause, close Gateway's sending gate first. For resume, send
            # the ordered WSS control frame first, then release the gate, so a
            # new Opus frame can never overtake tts.resume on the socket.
            if paused:
                session.playback_paused = True
                session.playback_pause_event.clear()
            try:
                await _send_control(
                    websocket,
                    session,
                    "tts.pause" if paused else "tts.resume",
                    turn_id=turn_id,
                    request_id=None,
                    payload={"generation_id": generation_id},
                )
            except Exception as exc:
                if paused:
                    session.playback_paused = False
                    session.playback_pause_event.set()
                await self.unregister(session)
                raise RuntimeError("playback control delivery failed") from exc
            if not paused:
                session.playback_paused = False
                session.playback_pause_event.set()
            return turn_id, generation_id

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
        try:
            await _send_control(
                websocket,
                session,
    "operator.control",
                turn_id=None,
                request_id=f"ctl_{uuid.uuid4().hex}",
                payload=command.payload(),
            )
        except Exception as exc:
            await self.unregister(session)
            raise RuntimeError("device control delivery failed") from exc

    async def dispatch_and_confirm(
        self, device_id: str, command: RemoteControlRequest
    ) -> OperatorControlAcknowledgement:
        """Deliver a browser control and wait for its matching ESP32 receipt."""
        request_id = f"ctl_{uuid.uuid4().hex}"
        loop = asyncio.get_running_loop()
        acknowledgement = loop.create_future()
        async with self._lock:
            connection = self._connections.get(device_id)
            if connection is None:
                raise RuntimeError("device is offline")
            websocket, session = connection
            if command.kind != "stop" and (
                session.is_listening
                or session.active_generation != 0
                or (
                    session.active_turn_task is not None
                    and not session.active_turn_task.done()
                )
            ):
                raise RuntimeError("device is busy with a voice turn")
            self._pending_operator_controls[(session.session_id, request_id)] = acknowledgement
        try:
            await _send_control(
                websocket,
                session,
                "operator.control",
                turn_id=None,
                request_id=request_id,
                payload=command.payload(),
            )
            return await asyncio.wait_for(
                asyncio.shield(acknowledgement),
                timeout=OPERATOR_CONTROL_ACK_TIMEOUT_SECONDS,
            )
        except TimeoutError as exc:
            async with self._lock:
                current = self._pending_operator_controls.get(
                    (session.session_id, request_id)
                )
                if current is acknowledgement:
                    del self._pending_operator_controls[(session.session_id, request_id)]
            raise RuntimeError("ESP32 did not acknowledge the control command") from exc
        except Exception as exc:
            async with self._lock:
                current = self._pending_operator_controls.get(
                    (session.session_id, request_id)
                )
                if current is acknowledgement:
                    del self._pending_operator_controls[(session.session_id, request_id)]
            if isinstance(exc, RuntimeError):
                raise
            await self.unregister(session)
            raise RuntimeError("device control delivery failed") from exc

    def complete_operator_control(
        self,
        session: DeviceSession,
        *,
        request_id: str | None,
        status: str,
        error_code: str | None,
    ) -> None:
        """Resolve only the browser request acknowledged by this ESP32 session."""
        if request_id is None:
            return
        acknowledgement = self._pending_operator_controls.pop(
            (session.session_id, request_id), None
        )
        if acknowledgement is not None and not acknowledgement.done():
            acknowledgement.set_result(
                OperatorControlAcknowledgement(
                    request_id=request_id,
                    status=status,
                    error_code=error_code,
                )
            )

    async def play_local_pcm_test(
        self,
        device_id: str,
        pcm: bytes,
        pipeline: VoicePipeline,
        observability: ObservabilityStore,
    ) -> tuple[int, int]:
        """Deliver a local PCM fixture through the production WSS downlink.

        The test firmware recognizes only the Gateway-created `test_` turn
        prefix, then validates the same response-plan, Opus, and TTS controls
        used for a regular device-initiated turn.
        """
        async with self._test_play_lock:
            async with self._lock:
                connection = self._connections.get(device_id)
            if connection is None:
                raise RuntimeError("device is offline")
            websocket, session = connection
            if (
                session.is_listening
                or session.active_generation != 0
                or (session.active_turn_task is not None and not session.active_turn_task.done())
            ):
                raise RuntimeError("device is busy with a voice turn")
            try:
                result = await _build_local_pcm_test_result(pipeline, pcm)
                turn_id = f"test_{uuid.uuid4().hex}"
                observability.record_stage(
                    device_id=device_id,
                    turn_id=turn_id,
                    stage="local_pcm_test",
                    status="started",
                    details={
                        "pcm_bytes": len(pcm),
                        "frame_count": len(result.opus_packets),
                        "generation_id": result.generation_id,
                    },
                )
                await _send_turn_result(
                    websocket,
                    session,
                    result,
                    turn_id=turn_id,
                    observability=observability,
                )
            except Exception as exc:
                await self.unregister(session)
                raise RuntimeError("local PCM test delivery failed") from exc
        return result.generation_id, len(result.opus_packets)


def _build_pipeline(
    settings: Settings, *, observer: ObservabilityStore | None = None
) -> ConversationPipeline | StreamingConversationPipeline:
    audio_format = AudioFormat()
    asr: AsrProvider
    agent: AgentProvider | None = None
    streaming_agent: OpenClawSseAgentProvider | None = None
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
    if settings.provider_mode == "openclaw_sse":
        streaming_agent = OpenClawSseAgentProvider(
            url=settings.openclaw_sse_url,
            token=settings.openclaw_token.get_secret_value(),
            agent_id=settings.openclaw_agent_id,
            session_key_secret=settings.openclaw_session_key_secret.get_secret_value(),
            timeout_seconds=settings.openclaw_timeout_seconds,
        )
    else:
        agent = OpenClawAgentProvider(
            url=settings.openclaw_url,
            token=load_openclaw_gateway_token(settings.openclaw_gateway_config_file),
            agent_id=settings.openclaw_agent_id,
            session_key_secret=settings.openclaw_session_key_secret.get_secret_value(),
            timeout_seconds=settings.openclaw_timeout_seconds,
            max_attempts=settings.openclaw_max_attempts,
            retry_initial_delay_seconds=settings.openclaw_retry_initial_delay_seconds,
            retry_max_delay_seconds=settings.openclaw_retry_max_delay_seconds,
            abort_timeout_seconds=settings.openclaw_abort_timeout_seconds,
            recv_timeout_seconds=settings.openclaw_recv_timeout_seconds,
            synthesis_timeout_seconds=settings.openclaw_synthesis_timeout_seconds,
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

    if streaming_agent is not None:
        return StreamingConversationPipeline(
            codec_factory=lambda: OpusCodec(audio_format),
            asr=asr,
            agent=streaming_agent,
            tts=tts,
            audio_format=audio_format,
            observer=observer,
        )
    if agent is None:
        raise ValueError("OpenClaw provider is not configured")
    return ConversationPipeline(
        codec_factory=lambda: OpusCodec(audio_format),
        asr=asr,
        agent=agent,
        tts=tts,
        audio_format=audio_format,
        observer=observer,
        web_search=web_search,
        recording_store=recording_store,
        manual_test_recordings_only=settings.test_recording_manual_only,
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
    pipeline: VoicePipeline | None = None,
    sandbox_reaper: SandboxReaper | None = None,
    mdns_advertiser: ServiceAdvertiser | None = None,
    observability: ObservabilityStore | None = None,
    device_controls: DeviceControlRegistry | None = None,
    local_pcm_tests: LocalPcmTestQueue | None = None,
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
    resolved_local_pcm_tests = local_pcm_tests or LocalPcmTestQueue()
    local_pcm_test_fixture_path = resolved_settings.local_pcm_test_fixture_path
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
    app.state.local_pcm_tests = resolved_local_pcm_tests

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

    @app.get("/test-console", response_class=HTMLResponse, include_in_schema=False)
    async def test_console(request: Request) -> HTMLResponse:
        _require_loopback_dashboard_access(request)
        html = files("sesame_voice_gateway").joinpath("test_console.html").read_text(
            encoding="utf-8"
        )
        return HTMLResponse(html, headers={"Cache-Control": "no-store"})

    @app.post(
        "/api/local-control/{device_id}",
        include_in_schema=False,
        status_code=202,
    )
    async def local_control(
        device_id: str,
        command: RemoteControlRequest,
        request: Request,
    ) -> JSONResponse:
        _require_loopback_dashboard_access(request)
        try:
            acknowledgement = await resolved_device_controls.dispatch_and_confirm(
                device_id, command
            )
        except RuntimeError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc
        return JSONResponse(
            {
                "status": acknowledgement.status,
                "device_id": device_id,
                "request_id": acknowledgement.request_id,
                "error_code": acknowledgement.error_code,
            },
            status_code=200 if acknowledgement.status == "accepted" else 409,
            headers={"Cache-Control": "no-store"},
        )

    @app.put(
        "/api/local-pcm-test/{device_id}",
        include_in_schema=False,
        status_code=202,
    )
    async def arm_local_pcm_test(device_id: str, request: Request) -> JSONResponse:
        """Arm one local 16 kHz mono S16LE fixture for the next device turn."""
        _require_loopback_dashboard_access(request)
        try:
            frame_count = await resolved_local_pcm_tests.arm(
                device_id, await request.body()
            )
        except LocalPcmTestError as exc:
            raise HTTPException(status_code=422, detail=str(exc)) from exc
        return JSONResponse(
            {
                "status": "armed",
                "device_id": device_id,
                "frame_count": frame_count,
                "duration_ms": frame_count * AudioFormat().frame_duration_ms,
            },
            status_code=202,
            headers={"Cache-Control": "no-store"},
        )

    async def play_armed_local_pcm_test(
        device_id: str, *, fixture: str | None = None
    ) -> JSONResponse:
        pcm = await resolved_local_pcm_tests.consume(device_id)
        if pcm is None:
            raise HTTPException(status_code=409, detail="no local PCM test is armed")
        try:
            generation_id, packet_count = await resolved_device_controls.play_local_pcm_test(
                device_id,
                pcm,
                resolved_pipeline,
                resolved_observability,
            )
        except RuntimeError as exc:
            await resolved_local_pcm_tests.arm(device_id, pcm)
            raise HTTPException(status_code=409, detail=str(exc)) from exc
        response: dict[str, Any] = {
            "status": "playing",
            "device_id": device_id,
            "generation_id": generation_id,
            "packet_count": packet_count,
            "duration_ms": packet_count * AudioFormat().frame_duration_ms,
        }
        if fixture is not None:
            response["fixture"] = fixture
        return JSONResponse(
            response,
            status_code=202,
            headers={"Cache-Control": "no-store"},
        )

    @app.post(
        "/api/local-pcm-test/{device_id}/play",
        include_in_schema=False,
        status_code=202,
    )
    async def play_local_pcm_test(device_id: str, request: Request) -> JSONResponse:
        """Play the armed fixture without a device BOOT-button event."""
        _require_loopback_dashboard_access(request)
        return await play_armed_local_pcm_test(device_id)

    @app.post(
        "/api/local-pcm-test/{device_id}/beijing-welcome/play",
        include_in_schema=False,
        status_code=202,
    )
    async def play_beijing_welcome_fixture(device_id: str, request: Request) -> JSONResponse:
        """Play the configured local fixture without a browser file upload."""
        _require_loopback_dashboard_access(request)
        if local_pcm_test_fixture_path is None:
            raise HTTPException(status_code=404, detail="the local test fixture is not configured")
        try:
            pcm = await asyncio.to_thread(local_pcm_test_fixture_path.read_bytes)
        except OSError as exc:
            raise HTTPException(
                status_code=503, detail="the local test fixture cannot be read"
            ) from exc
        try:
            await resolved_local_pcm_tests.arm(device_id, pcm)
        except LocalPcmTestError as exc:
            raise HTTPException(status_code=422, detail=str(exc)) from exc
        return await play_armed_local_pcm_test(device_id, fixture="beijing-welcome")

    async def set_local_playback_pause(
        device_id: str, request: Request, *, paused: bool
    ) -> JSONResponse:
        _require_loopback_dashboard_access(request)
        try:
            turn_id, generation_id = await resolved_device_controls.set_playback_paused(
                device_id, paused=paused
            )
        except RuntimeError as exc:
            raise HTTPException(status_code=409, detail=str(exc)) from exc
        resolved_observability.record_stage(
            device_id=device_id,
            turn_id=turn_id,
            stage="tts.playback.control",
            status="paused" if paused else "resumed",
            details={"generation_id": generation_id},
        )
        return JSONResponse(
            {
                "status": "paused" if paused else "playing",
                "device_id": device_id,
                "turn_id": turn_id,
                "generation_id": generation_id,
            },
            status_code=202,
            headers={"Cache-Control": "no-store"},
        )

    @app.post(
        "/api/local-pcm-test/{device_id}/pause",
        include_in_schema=False,
        status_code=202,
    )
    async def pause_local_pcm_test(device_id: str, request: Request) -> JSONResponse:
        return await set_local_playback_pause(device_id, request, paused=True)

    @app.post(
        "/api/local-pcm-test/{device_id}/resume",
        include_in_schema=False,
        status_code=202,
    )
    async def resume_local_pcm_test(device_id: str, request: Request) -> JSONResponse:
        return await set_local_playback_pause(device_id, request, paused=False)

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

    @app.websocket(resolved_settings.device_stream_path)
    async def device_stream(websocket: WebSocket) -> None:
        await _handle_device_stream(
            websocket=websocket,
            settings=resolved_settings,
            pipeline=resolved_pipeline,
            conversations=conversations,
            active_device_sessions=active_device_sessions,
            observability=resolved_observability,
            device_controls=resolved_device_controls,
            local_pcm_tests=resolved_local_pcm_tests,
        )

    return app


async def _handle_device_stream(
    *,
    websocket: WebSocket,
    settings: Settings,
    pipeline: VoicePipeline,
    conversations: ConversationRegistry,
    active_device_sessions: ActiveDeviceSessionRegistry,
    observability: ObservabilityStore,
    device_controls: DeviceControlRegistry,
    local_pcm_tests: LocalPcmTestQueue,
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
        await device_controls.register(websocket, session)
        await _receive_device_messages(
            websocket,
            session,
            pipeline,
            observability,
            device_controls,
            local_pcm_tests,
        )
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
            await device_controls.unregister(session)
            await active_device_sessions.release(session.device_id, session.session_id)


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

    A browser opening `https://sesame-stream-gateway.local` reaches the computer through
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
    pipeline: VoicePipeline,
    observability: ObservabilityStore,
    device_controls: DeviceControlRegistry,
    local_pcm_tests: LocalPcmTestQueue | None = None,
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
        await _handle_control_event(
            websocket,
            session,
            pipeline,
            event,
            observability,
            device_controls,
            local_pcm_tests,
        )


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


async def _build_local_pcm_test_result(
    pipeline: VoicePipeline, pcm: bytes
) -> TurnResult:
    """Encode an already-validated local fixture through the production codec."""
    audio_format = AudioFormat()
    frame_size = audio_format.pcm_bytes_per_frame
    codec = OpusCodec(audio_format)
    opus_packets = tuple(
        codec.encode_frame(pcm[offset : offset + frame_size])
        for offset in range(0, len(pcm), frame_size)
    )
    return TurnResult(
        transcript=AsrResult(text="local PCM transport test"),
        agent=AgentResult(text="local PCM transport test"),
        generation_id=await pipeline.next_generation_id(),
        opus_packets=opus_packets,
    )


async def _run_turn(
    *,
    websocket: WebSocket,
    session: DeviceSession,
    pipeline: VoicePipeline,
    context: ConversationContext,
    opus_packets: list[bytes],
    turn_epoch: int,
    observability: ObservabilityStore | None = None,
    local_pcm_tests: LocalPcmTestQueue | None = None,
) -> None:
    try:
        test_pcm = (
            await local_pcm_tests.consume(context.device_id)
            if local_pcm_tests is not None
            else None
        )
        if test_pcm is not None:
            result = await _build_local_pcm_test_result(pipeline, test_pcm)
            if observability is not None:
                observability.record_stage(
                    device_id=session.device_id,
                    turn_id=context.turn_id,
                    stage="local_pcm_test",
                    status="completed",
                    details={
                        "pcm_bytes": len(test_pcm),
                        "frame_count": len(test_pcm) // AudioFormat().pcm_bytes_per_frame,
                    },
                )
        elif isinstance(pipeline, StreamingConversationPipeline):
            await _send_stream_outputs(
                websocket,
                session,
                pipeline.stream_turn(context=context, opus_packets=opus_packets),
                turn_id=context.turn_id,
                turn_epoch=turn_epoch,
                observability=observability,
            )
            return
        else:
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
            await _flush_failed_stream(websocket, session, turn_id=context.turn_id)
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
            await _flush_failed_stream(websocket, session, turn_id=context.turn_id)
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
            await _flush_failed_stream(websocket, session, turn_id=context.turn_id)
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
            if isinstance(result, SilentDiscard):
                await _send_control(
                    websocket,
                    session,
                    "turn.complete",
                    turn_id=context.turn_id,
                    request_id=None,
                    payload={"outcome": "discard", "reason": result.reason},
                )
                if observability is not None:
                    observability.record_stage(
                        device_id=session.device_id,
                        turn_id=context.turn_id,
                        stage="turn.complete",
                        status="discarded",
                        details={"reason": result.reason},
                    )
            else:
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


async def _flush_failed_stream(
    websocket: WebSocket, session: DeviceSession, *, turn_id: str
) -> None:
    generation_id = session.active_generation
    if generation_id == 0:
        return
    await _send_control(
        websocket,
        session,
        "tts.flush",
        turn_id=turn_id,
        request_id=None,
        payload={"generation_id": generation_id},
    )
    session.active_generation = 0


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
    pipeline: VoicePipeline,
    event: ControlEvent,
    observability: ObservabilityStore | None = None,
    device_controls: DeviceControlRegistry | None = None,
    local_pcm_tests: LocalPcmTestQueue | None = None,
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
        session.capture_trigger = str(event.payload["trigger"])
        _clear_captured_audio(session)
        if observability is not None:
            observability.record_stage(
                device_id=session.device_id,
                turn_id=event.turn_id,
                stage="listen",
                status="started",
                details={"trigger": session.capture_trigger},
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
            capture_trigger=session.capture_trigger,
        )
        turn_epoch = session.turn_epoch + 1
        session.turn_epoch = turn_epoch
        opus_packets = list(session.opus_packets)
        uplink_generation_id = session.uplink_generation_id
        uplink_packet_count = session.uplink_packet_count
        uplink_audio_bytes = session.uplink_audio_bytes
        _clear_captured_audio(session)
        session.turn_id = None
        session.capture_trigger = "unknown"
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
                local_pcm_tests=local_pcm_tests,
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
        if event.turn_id is None and device_controls is not None:
            device_controls.complete_operator_control(
                session,
                request_id=event.request_id,
                status=str(event.payload["status"]),
                error_code=event.payload["error_code"],
            )
        if observability is not None:
            details = {
                "request_id": event.request_id,
                "error_code": event.payload["error_code"],
            }
            if event.turn_id is not None:
                observability.record_stage(
                    device_id=session.device_id,
                    turn_id=event.turn_id,
                    stage="action.execute",
                    status=str(event.payload["status"]),
                    details=details,
                )
            else:
                # Local operator controls do not belong to a voice turn. Record
                # their ESP32 acknowledgement as a device event so the control
                # console can distinguish real execution feedback from HTTP 202.
                observability.record_device_stage(
                    device_id=session.device_id,
                    stage="device.action",
                    status=str(event.payload["status"]),
                    details=details,
                    update_current_stage=False,
                )
        return

    if event.type == "playback.stats":
        generation_id = int(event.payload["generation_id"])
        if (
            event.turn_id is None
            or event.turn_id != session.playback_stats_turn_id
            or generation_id != session.playback_stats_generation
        ):
            raise ControlProtocolError(
                "playback.stats does not match the active downlink generation"
            )
        session.playback_stats = dict(event.payload)
        session.playback_stats_revision += 1
        session.playback_stats_event.set()
        logger.info(
            "playback_stats device_id=%s session_id=%s turn_id=%s generation_id=%d buffered_packets=%s underflows=%s",
            session.device_id,
            session.session_id,
            event.turn_id,
            generation_id,
            event.payload["buffered_packets"],
            event.payload["underflow_count"],
        )
        if observability is not None:
            observability.record_stage(
                device_id=session.device_id,
                turn_id=event.turn_id,
                stage="tts.playback.buffer",
                status=(
                    "completed"
                    if event.payload.get("playback_complete", False)
                    else "paused"
                    if event.payload["paused"]
                    else "playing"
                    if event.payload["playback_started"]
                    else "buffering"
                ),
                details=dict(event.payload),
            )
        return

    raise ControlProtocolError(f"event is not allowed from device: {event.type}")


@dataclass(slots=True)
class _DownlinkFlowControl:
    """Apply the ESP32's 20/40 packet watermarks without changing audio wire data."""

    generation_id: int
    bootstrap_packets: int
    high_watermark_packets: int = 40
    packets_sent: int = 0
    telemetry_seen: bool = False
    telemetry_wait_exhausted: bool = False
    bootstrap_confirmed: bool = False
    low_watermark_packets: int = 20
    _last_revision: int = -1
    _send_credit: int = 0
    _next_media_deadline: float | None = None

    def _refresh(self, session: DeviceSession) -> bool:
        if (
            session.playback_stats_generation != self.generation_id
            or session.playback_stats is None
            or session.playback_stats_revision == self._last_revision
        ):
            return False
        self._last_revision = session.playback_stats_revision
        self.telemetry_seen = True
        buffered_packets = int(session.playback_stats["buffered_packets"])
        max_buffered_packets = int(session.playback_stats["max_buffered_packets"])
        reported_low = int(session.playback_stats["low_watermark_packets"])
        reported_high = int(session.playback_stats["high_watermark_packets"])
        self.low_watermark_packets = reported_low
        self.high_watermark_packets = reported_high
        # A telemetry sample can arrive while WSS still has many bootstrap
        # frames in flight. Do not treat e.g. "4 packets received" as room to
        # send another 36 when 50 have already left Gateway. The ESP32's
        # monotonic max value is the acknowledgement that its jitter buffer
        # has actually received the complete startup lead.
        self.bootstrap_confirmed = (
            self.bootstrap_confirmed
            or max_buffered_packets >= self.bootstrap_packets
            or bool(session.playback_stats.get("playback_started", False))
        )
        if not self.bootstrap_confirmed:
            self._send_credit = 0
            return True
        # Stats arrive every 100 ms and WSS frames already in flight are not
        # visible in their sample. Limit a single acknowledgement to a small
        # refill burst: 5 normal frames, or 10 below low water. This still
        # prioritizes recovery below 400 ms but leaves headroom for the next
        # delayed measurement instead of overshooting the 60-frame buffer.
        refill_burst = 10 if buffered_packets < reported_low else 5
        self._send_credit = min(
            max(0, reported_high - buffered_packets), refill_burst
        )
        return True

    async def wait_before_send(
        self, session: DeviceSession, *, fallback_deadline: float
    ) -> None:
        while session.playback_paused:
            session.playback_pause_event.clear()
            # Re-check after clearing so a resume that arrives between the
            # loop condition and clear cannot be lost.
            if not session.playback_paused:
                break
            await session.playback_pause_event.wait()
        if self.packets_sent < self.bootstrap_packets:
            return

        self._refresh(session)
        if not self.telemetry_seen and not self.telemetry_wait_exhausted:
            # The bootstrap burst is bounded below the ESP32's 60-frame
            # capacity. Wait once for its first measurement. Older firmware
            # does not publish playback.stats; after this one grace period it
            # must use the absolute 20-ms fallback instead of adding 150 ms to
            # every remaining packet.
            session.playback_stats_event.clear()
            self._refresh(session)
            if not self.telemetry_seen:
                try:
                    await asyncio.wait_for(
                        session.playback_stats_event.wait(), timeout=0.15
                    )
                except TimeoutError:
                    pass
                self._refresh(session)
                if not self.telemetry_seen:
                    self.telemetry_wait_exhausted = True
        while self.telemetry_seen and self._send_credit <= 0:
            session.playback_stats_event.clear()
            # Re-check after clearing to avoid losing a just-arrived report.
            self._refresh(session)
            if self._send_credit > 0:
                break
            try:
                await asyncio.wait_for(session.playback_stats_event.wait(), timeout=0.25)
            except TimeoutError:
                # Do not deadlock if a legacy or temporarily busy device does
                # not publish telemetry. Absolute pacing retains the old safe
                # compatibility behavior while it recovers.
                break
            self._refresh(session)

        # The bootstrap reaches the 30-frame start threshold as a bounded
        # burst. Its first telemetry can arrive after the ESP32 has already
        # started consuming, so restart the media clock at the first refill.
        # Thereafter every post-bootstrap packet is exactly one 20-ms slot
        # apart. Credit permits a slot; it must never turn the refill into a
        # WSS burst that overflows the 60-frame queue.
        now = time.perf_counter()
        if self._next_media_deadline is None:
            self._next_media_deadline = now
        deadline = max(fallback_deadline, self._next_media_deadline)
        remaining_seconds = deadline - now
        if remaining_seconds > 0:
            await asyncio.sleep(remaining_seconds)

    def record_sent(self) -> None:
        if self.packets_sent >= self.bootstrap_packets:
            sent_at = time.perf_counter()
            self._next_media_deadline = max(
                self._next_media_deadline or sent_at, sent_at
            ) + AudioFormat().frame_duration_ms / 1_000
        self.packets_sent += 1
        if self.telemetry_seen and self.bootstrap_confirmed and self._send_credit > 0:
            self._send_credit -= 1


def _begin_downlink_flow(
    session: DeviceSession, *, generation_id: int, turn_id: str | None, fixed_music: bool
) -> _DownlinkFlowControl:
    session.playback_stats = None
    session.playback_stats_generation = generation_id
    session.playback_stats_turn_id = turn_id
    session.playback_stats_revision = 0
    session.playback_stats_event.clear()
    session.playback_paused = False
    session.playback_pause_event.set()
    # The ESP32 starts rendering at 30 packets. Sending a larger bootstrap can
    # never be acknowledged because the device consumes while later packets
    # are still in flight, which leaves the sender in repeated timeout pacing.
    # Use the same 600-ms lead for speech and deterministic local PCM, then
    # retain the device-reported 20/40 low/high refill watermarks.
    return _DownlinkFlowControl(
        generation_id=generation_id,
        bootstrap_packets=30,
    )


async def _wait_for_playback_complete(
    session: DeviceSession,
    *,
    generation_id: int,
    expected_frames: int,
    timeout_seconds: float = 2.0,
) -> bool:
    """Wait for ESP32 rendering, not merely the final WSS packet enqueue."""
    if session.playback_stats_generation != generation_id or expected_frames <= 0:
        return False

    def is_successful(stats: dict[str, Any]) -> bool:
        if not bool(stats.get("playback_complete", False)):
            return False
        return (
            int(stats.get("rendered_frames", -1)) == expected_frames
            and int(stats.get("underflow_count", -1)) == 0
            and int(stats.get("dropped_packet_count", -1)) == 0
            and int(stats.get("stale_generation_count", -1)) == 0
            and int(stats.get("out_of_order_count", -1)) == 0
            and int(stats.get("sequence_discontinuity_count", -1)) == 0
        )

    deadline = time.monotonic() + timeout_seconds
    first_stats_deadline = time.monotonic() + min(timeout_seconds, 0.25)
    while session.playback_stats_generation == generation_id:
        stats = session.playback_stats
        if stats is not None and bool(stats.get("playback_complete", False)):
            return is_successful(stats)
        remaining = deadline - time.monotonic()
        if stats is None:
            remaining = min(remaining, first_stats_deadline - time.monotonic())
        if remaining <= 0:
            return False
        session.playback_stats_event.clear()
        stats = session.playback_stats
        if stats is not None and bool(stats.get("playback_complete", False)):
            return is_successful(stats)
        try:
            await asyncio.wait_for(
                session.playback_stats_event.wait(), timeout=remaining
            )
        except TimeoutError:
            return False
    return False


@dataclass(slots=True)
class _DownlinkTiming:
    """Measure the actual cadence delivered to a device without changing it."""

    first_sent_at: float | None = None
    last_sent_at: float | None = None
    packet_count: int = 0
    last_interval_ms: int = 0
    interval_total_ms: int = 0
    max_interval_ms: int = 0
    max_lateness_ms: int = 0

    def record_packet_sent(self, sent_at: float) -> None:
        if self.first_sent_at is None:
            self.first_sent_at = sent_at
        elif self.last_sent_at is not None:
            self.last_interval_ms = max(
                0, round((sent_at - self.last_sent_at) * 1_000)
            )
            self.interval_total_ms += self.last_interval_ms
            self.max_interval_ms = max(self.max_interval_ms, self.last_interval_ms)

        expected_elapsed_ms = self.packet_count * 20
        actual_elapsed_ms = max(0, round((sent_at - self.first_sent_at) * 1_000))
        self.max_lateness_ms = max(
            self.max_lateness_ms,
            max(0, actual_elapsed_ms - expected_elapsed_ms),
        )
        self.last_sent_at = sent_at
        self.packet_count += 1

    def as_details(self) -> dict[str, int]:
        elapsed_ms = (
            0
            if self.first_sent_at is None or self.last_sent_at is None
            else max(0, round((self.last_sent_at - self.first_sent_at) * 1_000))
        )
        expected_elapsed_ms = max(0, self.packet_count - 1) * 20
        cumulative_drift_ms = elapsed_ms - expected_elapsed_ms
        return {
            "packet_count": self.packet_count,
            "elapsed_ms": elapsed_ms,
            "expected_elapsed_ms": expected_elapsed_ms,
            # Keep the legacy non-negative field and publish the signed value
            # used by the diagnostic page to expose early/late pacing alike.
            "drift_ms": max(0, cumulative_drift_ms),
            "cumulative_drift_ms": cumulative_drift_ms,
            "last_interval_ms": self.last_interval_ms,
            "average_interval_ms": (
                0 if self.packet_count <= 1 else round(self.interval_total_ms / (self.packet_count - 1))
            ),
            "max_interval_ms": self.max_interval_ms,
            "max_lateness_ms": self.max_lateness_ms,
        }


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
    # Defense in depth: OpenClaw output never becomes a physical action, even
    # if a nonstandard provider bypasses the client-side response normalizer.
    action = None
    action_request_id = None

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
    flow = _begin_downlink_flow(
        session,
        generation_id=result.generation_id,
        turn_id=turn_id,
        fixed_music=turn_id is not None and turn_id.startswith("test_"),
    )
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
    downlink_started_at = time.perf_counter()
    downlink_frame_interval_seconds = AudioFormat().frame_duration_ms / 1_000
    downlink_bytes = 0
    downlink_timing = _DownlinkTiming()
    for sequence, packet in enumerate(result.opus_packets):
        if turn_epoch is not None and not _turn_is_current(session, turn_epoch):
            return
        # Bootstrap into the ESP32's bounded Opus buffer, then begin the
        # normal media clock from its first refill.  The flow controller keeps
        # that clock continuous when telemetry arrives late or in bursts.
        deadline = downlink_started_at + max(
            0, sequence - flow.bootstrap_packets
        ) * downlink_frame_interval_seconds
        await flow.wait_before_send(session, fallback_deadline=deadline)
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
        downlink_timing.record_packet_sent(time.perf_counter())
        flow.record_sent()
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
    playback_confirmed = await _wait_for_playback_complete(
        session,
        generation_id=result.generation_id,
        expected_frames=len(result.opus_packets),
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
            details={
                "generation_id": result.generation_id,
                "transport_timing": downlink_timing.as_details(),
            },
        )
        observability.record_stage(
            device_id=session.device_id,
            turn_id=turn_id,
            stage="tts.playback",
            status="completed" if playback_confirmed else "unconfirmed",
            details={
                "generation_id": result.generation_id,
                "device_acknowledged": playback_confirmed,
            },
        )
    logger.info(
        "tts_completed device_id=%s session_id=%s generation_id=%d opus_packets=%d transport_timing=%s",
        session.device_id,
        session.session_id,
        result.generation_id,
        len(result.opus_packets),
        downlink_timing.as_details(),
    )
    if turn_epoch is None or _turn_is_current(session, turn_epoch):
        session.active_generation = 0


@dataclass(slots=True)
class _StreamingDownlink:
    generation_id: int
    next_sequence: int = 0
    started_at_ms: int = field(default_factory=lambda: _timestamp_ms())
    packet_count: int = 0
    byte_count: int = 0
    timing: _DownlinkTiming = field(default_factory=_DownlinkTiming)
    started_at_monotonic: float = field(default_factory=time.perf_counter)
    flow: _DownlinkFlowControl | None = None


async def _send_stream_outputs(
    websocket: WebSocket,
    session: DeviceSession,
    outputs: AsyncIterator[StreamOutput],
    *,
    turn_id: str,
    turn_epoch: int | None = None,
    observability: ObservabilityStore | None = None,
) -> None:
    """Send an already-validated stream without changing the v1 device wire format."""
    downlink: _StreamingDownlink | None = None
    async for output in outputs:
        if turn_epoch is not None and not _turn_is_current(session, turn_epoch):
            return
        if isinstance(output, SilentDiscard):
            if downlink is not None:
                raise RuntimeError("streaming output discarded after TTS started")
            await _send_control(
                websocket,
                session,
                "turn.complete",
                turn_id=turn_id,
                request_id=None,
                payload={"outcome": "discard", "reason": output.reason},
            )
            return
        if isinstance(output, StreamStart):
            if downlink is not None:
                raise RuntimeError("streaming output started more than once")
            downlink = await _start_stream_downlink(
                websocket,
                session,
                output,
                turn_id=turn_id,
                observability=observability,
            )
            continue
        if isinstance(output, StreamAudio):
            if downlink is None:
                raise RuntimeError("streaming audio arrived before response.plan")
            await _send_stream_audio(
                websocket,
                session,
                downlink,
                output,
                turn_id=turn_id,
                turn_epoch=turn_epoch,
                observability=observability,
            )
            continue
        if isinstance(output, StreamFinished):
            if downlink is None:
                raise RuntimeError("streaming reply finished before response.plan")
            await _finish_stream_downlink(
                websocket,
                session,
                downlink,
                turn_id=turn_id,
                final_expression=output.expression,
                observability=observability,
            )
            session.active_generation = 0
            return
        raise RuntimeError("streaming pipeline produced an unsupported output")
    if downlink is not None:
        raise RuntimeError("streaming reply ended without reply.final")


async def _start_stream_downlink(
    websocket: WebSocket,
    session: DeviceSession,
    output: StreamStart,
    *,
    turn_id: str,
    observability: ObservabilityStore | None,
) -> _StreamingDownlink:
    # `neutral` is the Gateway's semantic default. The unchanged v1 ESP32
    # catalog calls that safe idle face `idle`; no model value is applied here.
    expression_id = "idle"
    session.active_generation = output.generation_id
    flow = _begin_downlink_flow(
        session,
        generation_id=output.generation_id,
        turn_id=turn_id,
        fixed_music=False,
    )
    await _send_control(
        websocket,
        session,
        "response.plan",
        turn_id=turn_id,
        request_id=None,
        payload={
            "generation_id": output.generation_id,
            "expression_id": expression_id,
            "expression_ttl_ms": 3_000,
            "action_id": None,
            "action_request_id": None,
            "action_duration_ms": None,
            "action_deadline_ms": None,
        },
    )
    await _send_control(
        websocket,
        session,
        "tts.start",
        turn_id=turn_id,
        request_id=None,
        payload={"generation_id": output.generation_id},
    )
    if observability is not None:
        observability.record_stage(
            device_id=session.device_id,
            turn_id=turn_id,
            stage="response.plan",
            status="completed",
            details={"generation_id": output.generation_id, "expression": "neutral", "action": None},
        )
        observability.record_stage(
            device_id=session.device_id,
            turn_id=turn_id,
            stage="tts.downlink",
            status="started",
            details={"generation_id": output.generation_id, "streaming": True},
        )
    return _StreamingDownlink(generation_id=output.generation_id, flow=flow)


async def _send_stream_audio(
    websocket: WebSocket,
    session: DeviceSession,
    downlink: _StreamingDownlink,
    output: StreamAudio,
    *,
    turn_id: str,
    turn_epoch: int | None,
    observability: ObservabilityStore | None,
) -> None:
    for packet in output.opus_packets:
        if turn_epoch is not None and not _turn_is_current(session, turn_epoch):
            return
        bootstrap_packets = (
            downlink.flow.bootstrap_packets if downlink.flow is not None else 0
        )
        deadline = downlink.started_at_monotonic + (
            max(0, downlink.next_sequence - bootstrap_packets)
            * AudioFormat().frame_duration_ms
            / 1_000
        )
        if downlink.flow is not None:
            await downlink.flow.wait_before_send(
                session, fallback_deadline=deadline
            )
        else:
            remaining_seconds = deadline - time.perf_counter()
            if remaining_seconds > 0:
                await asyncio.sleep(remaining_seconds)
        await _send_binary(
            websocket,
            session,
            pack_audio_frame(
                AudioFrame(
                    direction=AudioDirection.DOWNLINK,
                    flags=0,
                    stream_id=2,
                    generation_id=downlink.generation_id,
                    sequence=downlink.next_sequence,
                    timestamp_ms=downlink.started_at_ms + downlink.next_sequence * 20,
                    payload=packet,
                )
            ),
        )
        downlink.timing.record_packet_sent(time.perf_counter())
        if downlink.flow is not None:
            downlink.flow.record_sent()
        downlink.next_sequence += 1
        downlink.packet_count += 1
        downlink.byte_count += len(packet)
    if observability is not None and downlink.packet_count:
        observability.record_audio_progress(
            device_id=session.device_id,
            turn_id=turn_id,
            direction="down",
            packet_count=downlink.packet_count,
            byte_count=downlink.byte_count,
        )


async def _finish_stream_downlink(
    websocket: WebSocket,
    session: DeviceSession,
    downlink: _StreamingDownlink,
    *,
    turn_id: str,
    final_expression: str,
    observability: ObservabilityStore | None,
) -> None:
    await _send_control(
        websocket,
        session,
        "tts.stop",
        turn_id=turn_id,
        request_id=None,
        payload={"generation_id": downlink.generation_id, "reason": "completed"},
    )
    playback_confirmed = await _wait_for_playback_complete(
        session,
        generation_id=downlink.generation_id,
        expected_frames=downlink.packet_count,
    )
    if observability is not None:
        observability.record_stage(
            device_id=session.device_id,
            turn_id=turn_id,
            stage="tts.downlink",
            status="completed",
            elapsed_ms=downlink.packet_count * 20,
            details={
                "generation_id": downlink.generation_id,
                "streaming": True,
                "transport_timing": downlink.timing.as_details(),
            },
        )
        observability.record_stage(
            device_id=session.device_id,
            turn_id=turn_id,
            stage="tts.playback",
            status="completed" if playback_confirmed else "unconfirmed",
            details={
                "generation_id": downlink.generation_id,
                "device_acknowledged": playback_confirmed,
            },
        )
    logger.info(
        "tts_stream_completed device_id=%s session_id=%s generation_id=%d opus_packets=%d transport_timing=%s",
        session.device_id,
        session.session_id,
        downlink.generation_id,
        downlink.packet_count,
        downlink.timing.as_details(),
    )


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
