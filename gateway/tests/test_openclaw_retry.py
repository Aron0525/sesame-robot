from __future__ import annotations

import asyncio
import time
import unittest
from unittest.mock import patch

from sesame_voice_gateway.openclaw.client import (
    OpenClawAgentProvider,
    OpenClawProtocolError,
    OpenClawUnavailableError,
    build_chat_abort_request,
    retry_transient_openclaw_operation,
)
from sesame_voice_gateway.app import DeviceSession, _run_turn
from sesame_voice_gateway.pipeline import ConversationContext
from sesame_voice_gateway.protocol.control import parse_control_event


class _FakeWebSocket:
    def __init__(self) -> None:
        self.text_frames: list[str] = []

    async def send_text(self, value: str) -> None:
        self.text_frames.append(value)


class _UnavailablePipeline:
    async def process_turn(self, **_: object) -> object:
        raise OpenClawUnavailableError("OpenClaw is temporarily unavailable")


class OpenClawRetryTest(unittest.IsolatedAsyncioTestCase):
    async def test_retries_a_transient_network_failure_with_exponential_backoff(self) -> None:
        attempts = 0
        delays: list[float] = []

        async def operation() -> str:
            nonlocal attempts
            attempts += 1
            if attempts < 3:
                raise OSError("temporary local socket failure")
            return "completed"

        async def record_delay(seconds: float) -> None:
            delays.append(seconds)

        result = await retry_transient_openclaw_operation(
            operation,
            max_attempts=3,
            initial_delay_seconds=0.25,
            sleep=record_delay,
        )

        self.assertEqual(result, "completed")
        self.assertEqual(attempts, 3)
        self.assertEqual(delays, [0.25, 0.5])

    async def test_does_not_retry_a_protocol_error(self) -> None:
        attempts = 0

        async def operation() -> str:
            nonlocal attempts
            attempts += 1
            raise OpenClawProtocolError("invalid response schema")

        with self.assertRaisesRegex(OpenClawProtocolError, "invalid response schema"):
            await retry_transient_openclaw_operation(
                operation,
                max_attempts=3,
                initial_delay_seconds=0.25,
            )

        self.assertEqual(attempts, 1)

    async def test_caps_retry_delay(self) -> None:
        attempts = 0
        delays: list[float] = []

        async def operation() -> str:
            nonlocal attempts
            attempts += 1
            if attempts < 4:
                raise OSError("temporary local socket failure")
            return "completed"

        async def record_delay(seconds: float) -> None:
            delays.append(seconds)

        result = await retry_transient_openclaw_operation(
            operation,
            max_attempts=4,
            initial_delay_seconds=0.25,
            max_delay_seconds=0.5,
            sleep=record_delay,
        )

        self.assertEqual(result, "completed")
        self.assertEqual(delays, [0.25, 0.5, 0.5])

    async def test_exhausted_transport_failure_becomes_safe_provider_error(self) -> None:
        provider = OpenClawAgentProvider(
            url="ws://127.0.0.1:18789",
            token="test-token",
            session_key_secret="test-secret",
            timeout_seconds=1,
            max_attempts=2,
            retry_initial_delay_seconds=0.001,
        )
        attempts = 0

        async def unavailable(_provider: OpenClawAgentProvider, **_: object) -> str:
            nonlocal attempts
            attempts += 1
            raise OSError("local socket unavailable")

        with patch.object(OpenClawAgentProvider, "_run_chat_attempt", new=unavailable):
            with self.assertRaises(OpenClawUnavailableError):
                await provider._run_chat(
                    prompt="test prompt",
                    session_key="agent:sesame:conversation:test",
                    idempotency_key="turn_001",
                )

        self.assertEqual(attempts, 2)

    async def test_cancel_aborts_only_the_current_run(self) -> None:
        provider = OpenClawAgentProvider(
            url="ws://127.0.0.1:18789",
            token="test-token",
            session_key_secret="test-secret",
        )
        started = asyncio.Event()
        aborted: list[tuple[str, str]] = []

        async def wait_forever(_provider: OpenClawAgentProvider, **_: object) -> str:
            started.set()
            await asyncio.Event().wait()
            raise AssertionError("unreachable")

        async def record_abort(
            _provider: OpenClawAgentProvider, *, session_key: str, run_id: str
        ) -> None:
            aborted.append((session_key, run_id))

        with patch.object(OpenClawAgentProvider, "_run_chat_attempt", new=wait_forever):
            with patch.object(
                OpenClawAgentProvider,
                "_abort_run_best_effort",
                new=record_abort,
            ):
                task = asyncio.create_task(
                    provider._run_chat(
                        prompt="test prompt",
                        session_key="agent:sesame:conversation:test",
                        idempotency_key="turn_001",
                    )
                )
                await asyncio.wait_for(started.wait(), timeout=1)
                task.cancel()

                with self.assertRaises(asyncio.CancelledError):
                    await task

        self.assertEqual(
            aborted,
            [("agent:sesame:conversation:test", "turn_001")],
        )

    async def test_total_timeout_covers_all_attempts_and_requests_abort(self) -> None:
        provider = OpenClawAgentProvider(
            url="ws://127.0.0.1:18789",
            token="test-token",
            session_key_secret="test-secret",
            timeout_seconds=0.02,
            max_attempts=3,
            retry_initial_delay_seconds=0.001,
        )
        attempts = 0
        aborted: list[tuple[str, str]] = []

        async def slow_attempt(_provider: OpenClawAgentProvider, **_: object) -> str:
            nonlocal attempts
            attempts += 1
            await asyncio.sleep(1)
            raise AssertionError("unreachable")

        async def record_abort(
            _provider: OpenClawAgentProvider, *, session_key: str, run_id: str
        ) -> None:
            aborted.append((session_key, run_id))

        started_at = time.monotonic()
        with patch.object(OpenClawAgentProvider, "_run_chat_attempt", new=slow_attempt):
            with patch.object(
                OpenClawAgentProvider,
                "_abort_run_best_effort",
                new=record_abort,
            ):
                with self.assertRaises(OpenClawUnavailableError):
                    await provider._run_chat(
                        prompt="test prompt",
                        session_key="agent:sesame:conversation:test",
                        idempotency_key="turn_001",
                    )

        self.assertLess(time.monotonic() - started_at, 0.2)
        self.assertEqual(attempts, 1)
        self.assertEqual(
            aborted,
            [("agent:sesame:conversation:test", "turn_001")],
        )

    def test_abort_request_always_names_one_run(self) -> None:
        request = build_chat_abort_request(
            request_id="abort_001",
            session_key="agent:sesame:conversation:test",
            run_id="turn_001",
        )

        self.assertEqual(request["method"], "chat.abort")
        self.assertEqual(
            request["params"],
            {
                "sessionKey": "agent:sesame:conversation:test",
                "runId": "turn_001",
            },
        )

    async def test_device_receives_a_retryable_openclaw_error(self) -> None:
        websocket = _FakeWebSocket()
        session = DeviceSession(
            device_id="dev_001",
            user_id="usr_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        context = ConversationContext(
            device_id="dev_001",
            user_id="usr_001",
            conversation_id="conv_001",
            turn_id="turn_001",
        )
        task = asyncio.create_task(
            _run_turn(
                websocket=websocket,  # type: ignore[arg-type]
                session=session,
                pipeline=_UnavailablePipeline(),  # type: ignore[arg-type]
                context=context,
                opus_packets=[b"opus"],
                turn_epoch=1,
            )
        )
        session.active_turn_task = task
        session.turn_epoch = 1
        await task

        event = parse_control_event(websocket.text_frames[0])
        self.assertEqual(event.type, "error")
        self.assertEqual(event.payload["code"], "openclaw_unavailable")
        self.assertTrue(event.payload["retryable"])


if __name__ == "__main__":
    unittest.main()
