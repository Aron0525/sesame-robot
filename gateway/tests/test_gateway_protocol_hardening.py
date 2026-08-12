from __future__ import annotations

import asyncio
import unittest
from unittest.mock import patch

from fastapi import WebSocketDisconnect

from sesame_voice_gateway.app import (
    ActiveDeviceSessionRegistry,
    DeviceSession,
    _authenticate_session,
    _handle_control_event,
    _receive_audio,
    _run_turn,
    _send_turn_result,
)
from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.conversations import (
    ConversationOwnershipError,
    ConversationRegistry,
)
from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.pipeline import ConversationContext, TurnResult
from sesame_voice_gateway.protocol.audio import (
    AUDIO_FLAG_PCM_S16LE,
    AudioDirection,
    AudioFrame,
    AudioProtocolError,
    pack_audio_frame,
)
from sesame_voice_gateway.protocol.control import (
    ControlEvent,
    ControlProtocolError,
    MAX_CONTROL_FRAME_BYTES,
    parse_control_event,
    serialize_control_event,
)
from sesame_voice_gateway.providers.base import (
    ActionSpec,
    AgentResult,
    AsrResult,
    ExpressionSpec,
    VoiceSpec,
)
from sesame_voice_gateway.server_cli import build_uvicorn_options


class FakeWebSocket:
    def __init__(self, *, authorization: str | None = None, incoming: str | None = None) -> None:
        self.headers = {} if authorization is None else {"authorization": authorization}
        self.incoming = incoming
        self.text_frames: list[str] = []
        self.binary_frames: list[bytes] = []
        self.closed: tuple[int, str] | None = None

    async def receive(self) -> dict[str, object]:
        if self.incoming is None:
            raise AssertionError("test websocket received an unexpected read")
        return {"text": self.incoming}

    async def send_text(self, value: str) -> None:
        self.text_frames.append(value)

    async def send_bytes(self, value: bytes) -> None:
        self.binary_frames.append(value)

    async def close(self, *, code: int, reason: str) -> None:
        self.closed = (code, reason)


class BrokenPipeline:
    async def process_turn(self, **_: object) -> TurnResult:
        raise KeyError("private user audio or transcript")


def _settings(*, device_tokens: dict[str, str], device_users: dict[str, str]) -> Settings:
    return Settings(
        _env_file=None,
        gateway_id="gw_test",
        device_tokens=device_tokens,
        device_users=device_users,
        allow_remote_speech=True,
        dashscope_api_key="test-key",
        openclaw_token="test-token",
        openclaw_session_key_secret="test-session-secret",
    )


def _hello(*, device_id: str, conversation_id: str | None, sequence: int = 0) -> str:
    return serialize_control_event(
        ControlEvent(
            v=1,
            type="session.hello",
            session_id=None,
            turn_id=None,
            request_id=None,
            sequence=sequence,
            timestamp_ms=1_000,
            payload={
                "device_id": device_id,
                "gateway_id": "gw_test",
                "conversation_id": conversation_id,
                "protocol_version": 1,
                "audio": {
                    "codec": "opus",
                    "sample_rate": 16_000,
                    "channels": 1,
                    "frame_duration_ms": 20,
                },
            },
        )
    )


def _control_event(event_type: str, *, sequence: int, payload: dict[str, object] | None = None) -> ControlEvent:
    resolved_payload = (
        {"trigger": "manual"} if event_type == "listen.start" and payload is None else payload or {}
    )
    return ControlEvent(
        v=1,
        type=event_type,  # type: ignore[arg-type]
        session_id="ses_001",
        turn_id="turn_001" if event_type.startswith("listen.") else None,
        request_id="act_001" if event_type == "action.result" else None,
        sequence=sequence,
        timestamp_ms=1_000,
        payload=resolved_payload,
    )


def _session(*, listening: bool = True) -> DeviceSession:
    return DeviceSession(
        device_id="device_001",
        user_id="user_001",
        session_id="ses_001",
        conversation_id="conv_001",
        is_listening=listening,
        turn_id="turn_001" if listening else None,
    )


def _uplink_audio(
    *,
    sequence: int,
    stream_id: int = 1,
    generation_id: int = 1,
    flags: int = 0,
    payload: bytes = b"opus",
) -> bytes:
    return pack_audio_frame(
        AudioFrame(
            direction=AudioDirection.UPLINK,
            flags=flags,
            stream_id=stream_id,
            generation_id=generation_id,
            sequence=sequence,
            timestamp_ms=sequence * 20,
            payload=payload,
        )
    )


class GatewayProtocolHardeningTest(unittest.IsolatedAsyncioTestCase):
    async def test_authenticated_unknown_or_expired_conversation_is_reissued(self) -> None:
        clock = [0.0]
        conversations = ConversationRegistry(ttl_seconds=60, clock=lambda: clock[0])
        old_conversation = conversations.resolve(
            requested_conversation_id=None,
            device_id="device_001",
            user_id="user_001",
        )
        clock[0] = 61.0
        websocket = FakeWebSocket(
            authorization="Bearer device-token",
            incoming=_hello(device_id="device_001", conversation_id=old_conversation.conversation_id),
        )

        active_sessions = ActiveDeviceSessionRegistry()
        session = await _authenticate_session(
            websocket,
            _settings(device_tokens={"device_001": "device-token"}, device_users={"device_001": "user_001"}),
            conversations,
            active_sessions,
        )

        self.assertNotEqual(session.conversation_id, old_conversation.conversation_id)
        self.assertTrue(await active_sessions.release(session.device_id, session.session_id))

    async def test_foreign_conversation_is_not_reissued(self) -> None:
        conversations = ConversationRegistry(ttl_seconds=60)
        foreign = conversations.resolve(
            requested_conversation_id=None,
            device_id="device_a",
            user_id="user_a",
        )
        websocket = FakeWebSocket(
            authorization="Bearer device-token-b",
            incoming=_hello(device_id="device_b", conversation_id=foreign.conversation_id),
        )

        with self.assertRaises(ConversationOwnershipError):
            await _authenticate_session(
                websocket,
                _settings(device_tokens={"device_b": "device-token-b"}, device_users={"device_b": "user_b"}),
                conversations,
                ActiveDeviceSessionRegistry(),
            )

    async def test_hello_must_start_control_sequence_at_zero(self) -> None:
        websocket = FakeWebSocket(
            authorization="Bearer device-token",
            incoming=_hello(device_id="device_001", conversation_id=None, sequence=1),
        )

        with self.assertRaises(ControlProtocolError):
            await _authenticate_session(
                websocket,
                _settings(device_tokens={"device_001": "device-token"}, device_users={"device_001": "user_001"}),
                ConversationRegistry(ttl_seconds=60),
                ActiveDeviceSessionRegistry(),
            )

    async def test_audited_control_identifiers_cannot_contain_free_text(self) -> None:
        malformed = ControlEvent(
            v=1,
            type="listen.start",
            session_id="ses_001",
            turn_id="private words must not enter audit logs",
            request_id=None,
            sequence=1,
            timestamp_ms=1_000,
            payload={},
        )

        with self.assertRaises(ControlProtocolError):
            parse_control_event(serialize_control_event(malformed))

    async def test_only_one_live_connection_can_claim_a_device(self) -> None:
        registry = ActiveDeviceSessionRegistry()

        self.assertTrue(await registry.claim("device_001", "ses_one"))
        self.assertFalse(await registry.claim("device_001", "ses_two"))
        self.assertFalse(await registry.release("device_001", "ses_two"))
        self.assertTrue(await registry.release("device_001", "ses_one"))
        self.assertTrue(await registry.claim("device_001", "ses_two"))

    async def test_second_authenticated_connection_is_rejected_for_the_same_device(self) -> None:
        settings = _settings(
            device_tokens={"device_001": "device-token"},
            device_users={"device_001": "user_001"},
        )
        conversations = ConversationRegistry(ttl_seconds=60)
        active_sessions = ActiveDeviceSessionRegistry()
        first_websocket = FakeWebSocket(
            authorization="Bearer device-token",
            incoming=_hello(device_id="device_001", conversation_id=None),
        )
        first_session = await _authenticate_session(
            first_websocket, settings, conversations, active_sessions
        )
        second_websocket = FakeWebSocket(
            authorization="Bearer device-token",
            incoming=_hello(
                device_id="device_001", conversation_id=first_session.conversation_id
            ),
        )

        class MustNotResolveConversation:
            def resolve_or_reissue_for_authenticated_device(self, **_: object) -> object:
                raise AssertionError("a duplicate device must be rejected before conversation work")

        with self.assertRaises(WebSocketDisconnect):
            await _authenticate_session(
                second_websocket,
                settings,
                MustNotResolveConversation(),  # type: ignore[arg-type]
                active_sessions,
            )

        self.assertEqual(second_websocket.closed, (4409, "device already has an active session"))
        self.assertTrue(
            await active_sessions.release(first_session.device_id, first_session.session_id)
        )

    async def test_websocket_transport_has_an_outer_frame_size_limit(self) -> None:
        options = build_uvicorn_options(
            _settings(
                device_tokens={"device_001": "device-token"},
                device_users={"device_001": "user_001"},
            )
        )

        self.assertEqual(options["ws_max_size"], MAX_CONTROL_FRAME_BYTES)

    async def test_device_control_sequence_is_strict_and_replay_safe(self) -> None:
        websocket = FakeWebSocket()
        session = _session(listening=False)

        with self.assertRaisesRegex(ControlProtocolError, "sequence"):
            await _handle_control_event(
                websocket,
                session,
                BrokenPipeline(),
                _control_event("listen.start", sequence=2),
            )

        await _handle_control_event(
            websocket,
            session,
            BrokenPipeline(),
            _control_event("listen.start", sequence=1),
        )
        with self.assertRaisesRegex(ControlProtocolError, "sequence"):
            await _handle_control_event(
                websocket,
                session,
                BrokenPipeline(),
                _control_event("listen.stop", sequence=1),
            )

    async def test_listen_start_requires_a_capture_trigger(self) -> None:
        with self.assertRaises(ControlProtocolError):
            parse_control_event(
                serialize_control_event(
                    _control_event("listen.start", sequence=1, payload={})
                )
            )

        event = parse_control_event(
            serialize_control_event(
                _control_event(
                    "listen.start", sequence=1, payload={"trigger": "manual"}
                )
            )
        )
        self.assertEqual(event.payload["trigger"], "manual")

        followup = parse_control_event(
            serialize_control_event(
                _control_event(
                    "listen.start", sequence=2, payload={"trigger": "followup"}
                )
            )
        )
        self.assertEqual(followup.payload["trigger"], "followup")

    async def test_audio_requires_opus_uplink_stream_one_and_single_generation(self) -> None:
        session = _session()

        with self.assertRaises(AudioProtocolError):
            _receive_audio(session, _uplink_audio(sequence=0, flags=AUDIO_FLAG_PCM_S16LE))
        with self.assertRaises(AudioProtocolError):
            _receive_audio(session, _uplink_audio(sequence=0, stream_id=2))
        with self.assertRaises(AudioProtocolError):
            _receive_audio(session, _uplink_audio(sequence=0, generation_id=0))

        _receive_audio(session, _uplink_audio(sequence=0, generation_id=7))
        with self.assertRaisesRegex(AudioProtocolError, "generation"):
            _receive_audio(session, _uplink_audio(sequence=1, generation_id=8))

    async def test_audio_is_bounded_before_pipeline_processing(self) -> None:
        session = _session()
        for sequence in range(1_500):
            _receive_audio(session, _uplink_audio(sequence=sequence, payload=b"x"))

        with self.assertRaisesRegex(AudioProtocolError, "duration"):
            _receive_audio(session, _uplink_audio(sequence=1_500, payload=b"x"))

    async def test_response_plan_omits_an_agent_action_and_its_deadline(self) -> None:
        websocket = FakeWebSocket()
        session = _session(listening=False)
        result = TurnResult(
            transcript=AsrResult(text="synthetic transcript"),
            agent=AgentResult(
                text="synthetic reply",
                voice=VoiceSpec(),
                expression=ExpressionSpec(name="happy", ttl_ms=1_000),
                actions=(ActionSpec(name="wave", duration_ms=1_000),),
            ),
            generation_id=4,
            opus_packets=(b"one",),
        )
        timestamps = iter((10_000, 20_000, 30_000, 40_000, 50_000))

        with patch("sesame_voice_gateway.app._timestamp_ms", side_effect=lambda: next(timestamps)):
            await _send_turn_result(websocket, session, result)

        response_plan = parse_control_event(websocket.text_frames[0])
        self.assertIsNone(response_plan.payload["action_id"])
        self.assertIsNone(response_plan.payload["action_request_id"])
        self.assertIsNone(response_plan.payload["action_duration_ms"])
        self.assertIsNone(response_plan.payload["action_deadline_ms"])

    async def test_unexpected_provider_error_returns_only_generic_error(self) -> None:
        websocket = FakeWebSocket()
        session = _session(listening=False)
        context = ConversationContext(
            device_id=session.device_id,
            user_id=session.user_id,
            conversation_id=session.conversation_id,
            turn_id="turn_001",
        )

        async def run() -> None:
            session.active_turn_task = asyncio.current_task()
            await _run_turn(
                websocket=websocket,
                session=session,
                pipeline=BrokenPipeline(),
                context=context,
                opus_packets=[b"opus"],
                turn_epoch=session.turn_epoch,
            )

        await run()
        event = parse_control_event(websocket.text_frames[0])
        self.assertEqual(event.type, "error")
        self.assertEqual(event.payload["code"], "voice_pipeline_failed")
        self.assertNotIn("private", event.payload["message"])

    async def test_operator_control_ack_is_visible_without_a_voice_turn(self) -> None:
        websocket = FakeWebSocket()
        session = _session(listening=False)
        observability = ObservabilityStore(max_events=5, clock=lambda: 1_000)

        await _handle_control_event(
            websocket,
            session,
            BrokenPipeline(),
            ControlEvent(
                v=1,
                type="action.result",
                session_id=session.session_id,
                turn_id=None,
                request_id="ctl_001",
                sequence=1,
                timestamp_ms=1_000,
                payload={"status": "accepted", "error_code": None},
            ),
            observability,
        )

        events = observability.snapshot()["events"]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["stage"], "device.action")
        self.assertEqual(events[0]["status"], "accepted")
        self.assertEqual(
            events[0]["details"],
            {"request_id": "ctl_001", "error_code": None},
        )

    async def test_action_result_is_a_metadata_only_audit_event(self) -> None:
        websocket = FakeWebSocket()
        session = _session(listening=False)
        with self.assertLogs("sesame_voice_gateway.app", level="INFO") as logs:
            await _handle_control_event(
                websocket,
                session,
                BrokenPipeline(),
                _control_event(
                    "action.result",
                    sequence=1,
                    payload={"status": "completed", "error_code": "private detail"},
                ),
            )

        log_output = "\n".join(logs.output)
        self.assertIn("action_result", log_output)
        self.assertNotIn("private detail", log_output)


if __name__ == "__main__":
    unittest.main()
