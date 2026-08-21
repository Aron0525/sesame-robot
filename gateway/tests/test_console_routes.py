from __future__ import annotations

import unittest

from fastapi.testclient import TestClient

from sesame_voice_gateway.app import (
    DeviceControlRegistry,
    DeviceSession,
    OperatorControlAcknowledgement,
    RemoteControlRequest,
    create_app,
)
from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.protocol.control import parse_control_event


class RecordingDeviceControls:
    def __init__(self) -> None:
        self.commands: list[tuple[str, dict[str, object]]] = []

    async def dispatch(self, device_id: str, command: object) -> None:
        self.commands.append((device_id, command.model_dump()))  # type: ignore[union-attr]

    async def dispatch_and_confirm(
        self, device_id: str, command: object
    ) -> OperatorControlAcknowledgement:
        await self.dispatch(device_id, command)
        return OperatorControlAcknowledgement(
            request_id="ctl_test_acknowledged", status="accepted", error_code=None
        )


class CapturingWebSocket:
    def __init__(self) -> None:
        self.frames: list[str] = []

    async def send_text(self, frame: str) -> None:
        self.frames.append(frame)


class ConsoleRoutesTest(unittest.TestCase):
    def setUp(self) -> None:
        settings = Settings(
            _env_file=None,
            device_tokens={"device": "token"},
            device_users={"device": "user"},
            allow_remote_speech=True,
            dashscope_api_key="test-key",
            openclaw_token="test-token",
            openclaw_session_key_secret="test-secret",
        )
        self.controls = RecordingDeviceControls()
        self.app = create_app(
            settings,
            pipeline=object(),  # type: ignore[arg-type]
            observability=ObservabilityStore(max_events=20),
            device_controls=self.controls,  # type: ignore[arg-type]
        )

    def test_console_and_local_control_are_available_only_to_the_local_browser(self) -> None:
        with TestClient(self.app, client=("127.0.0.1", 4321)) as client:
            console = client.get("/console")
            response = client.post(
                "/api/local-control/dev_001",
                json={"kind": "action", "action": "wave"},
            )

        self.assertEqual(console.status_code, 200)
        self.assertIn("CONTROL", console.text)
        self.assertIn("17 ACTIONS", console.text)
        self.assertIn("挥手", console.text)
        self.assertIn("开心说话", console.text)
        self.assertIn("$('face-status').textContent", console.text)
        self.assertIn("手动舵机", console.text)
        self.assertIn("GAMEPAD", console.text)
        self.assertIn("设备状态", console.text)
        self.assertIn("回合运行状态", console.text)
        self.assertIn("语音文本", console.text)
        self.assertIn("ASR 识别", console.text)
        self.assertIn("OpenClaw 回复", console.text)
        self.assertIn("最新下发计划", console.text)
        self.assertIn("事件时间线", console.text)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["status"], "accepted")
        self.assertEqual(response.json()["request_id"], "ctl_test_acknowledged")
        self.assertEqual(
            self.controls.commands,
            [
                (
                    "dev_001",
                    {
                        "kind": "action",
                        "action": "wave",
                        "expression": None,
                        "servo": None,
                        "angle": None,
                        "frame_delay_ms": None,
                        "walk_cycles": None,
                        "motor_current_delay_ms": None,
                        "wake_threshold_hundredths": None,
                        "speaker_verification_enabled": None,
                    },
                )
            ],
        )

    def test_local_control_rejects_removed_actions_but_keeps_their_faces(self) -> None:
        for action in ("point", "bow"):
            with self.assertRaises(ValueError):
                RemoteControlRequest(kind="action", action=action)

        for expression in ("point", "bow"):
            self.assertEqual(
                RemoteControlRequest(kind="expression", expression=expression).expression,
                expression,
            )

    def test_local_control_rejects_a_lan_browser(self) -> None:
        with TestClient(self.app, client=("192.168.88.23", 4321)) as client:
            response = client.post(
                "/api/local-control/dev_001",
                json={"kind": "stop"},
            )

        self.assertEqual(response.status_code, 403)

    def test_local_control_accepts_expression_and_motion_settings(self) -> None:
        with TestClient(self.app, client=("127.0.0.1", 4321)) as client:
            expression = client.post(
                "/api/local-control/dev_001",
                json={"kind": "expression", "expression": "talk_happy"},
            )
            settings = client.post(
                "/api/local-control/dev_001",
                json={
                    "kind": "settings",
                    "frame_delay_ms": 100,
                    "walk_cycles": 10,
                    "motor_current_delay_ms": 20,
                },
            )

        self.assertEqual(expression.status_code, 200)
        self.assertEqual(settings.status_code, 200)
        self.assertEqual(
            self.controls.commands[-2:],
            [
                (
                    "dev_001",
                    {
                        "kind": "expression",
                        "action": None,
                        "expression": "talk_happy",
                        "servo": None,
                        "angle": None,
                        "frame_delay_ms": None,
                        "walk_cycles": None,
                        "motor_current_delay_ms": None,
                        "wake_threshold_hundredths": None,
                        "speaker_verification_enabled": None,
                    },
                ),
                (
                    "dev_001",
                    {
                        "kind": "settings",
                        "action": None,
                        "expression": None,
                        "servo": None,
                        "angle": None,
                        "frame_delay_ms": 100,
                        "walk_cycles": 10,
                        "motor_current_delay_ms": 20,
                        "wake_threshold_hundredths": None,
                        "speaker_verification_enabled": None,
                    },
                ),
            ],
        )

    def test_console_accepts_a_hundredth_precision_wake_threshold(self) -> None:
        with TestClient(self.app, client=("127.0.0.1", 4321)) as client:
            console = client.get("/console")
            response = client.post(
                "/api/local-control/dev_001",
                json={"kind": "wakeword_settings", "wake_threshold_hundredths": 31},
            )

        self.assertIn('id="wake-threshold"', console.text)
        self.assertIn('id="wake-threshold-slider"', console.text)
        self.assertIn('step="0.01"', console.text)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(
            self.controls.commands[-1],
            (
                "dev_001",
                {
                    "kind": "wakeword_settings",
                    "action": None,
                    "expression": None,
                    "servo": None,
                    "angle": None,
                    "frame_delay_ms": None,
                    "walk_cycles": None,
                    "motor_current_delay_ms": None,
                    "wake_threshold_hundredths": 31,
                    "speaker_verification_enabled": None,
                },
            ),
        )

    def test_console_preserves_local_speaker_verification_control(self) -> None:
        with TestClient(self.app, client=("127.0.0.1", 4321)) as client:
            console = client.get("/console")
            response = client.post(
                "/api/local-control/dev_001",
                json={
                    "kind": "speaker_verification_settings",
                    "speaker_verification_enabled": True,
                },
            )

        self.assertIn('id="enable-speaker-verification"', console.text)
        self.assertIn('id="disable-speaker-verification"', console.text)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(
            self.controls.commands[-1][1]["speaker_verification_enabled"], True
        )

    def test_device_dispatch_uses_the_existing_control_protocol(self) -> None:
        async def exercise() -> None:
            registry = DeviceControlRegistry()
            websocket = CapturingWebSocket()
            session = DeviceSession(
                device_id="dev_001",
                user_id="operator",
                session_id="ses_001",
                conversation_id="conv_001",
            )
            await registry.register(websocket, session)  # type: ignore[arg-type]
            await registry.dispatch(
                "dev_001", RemoteControlRequest(kind="action", action="wave")
            )
            await registry.dispatch(
                "dev_001", RemoteControlRequest(kind="expression", expression="happy")
            )
            await registry.dispatch(
                "dev_001", RemoteControlRequest(kind="servo", servo=1, angle=90)
            )

            events = [parse_control_event(frame) for frame in websocket.frames]
            self.assertEqual([event.type for event in events], ["operator.control"] * 3)
            self.assertEqual([event.turn_id for event in events], [None] * 3)
            self.assertEqual(len({event.request_id for event in events}), 3)
            self.assertTrue(all(event.request_id.startswith("ctl_") for event in events))
            self.assertEqual(events[0].payload, {"kind": "action", "action": "wave"})
            self.assertEqual(events[1].payload, {"kind": "expression", "expression": "happy"})
            self.assertEqual(events[2].payload, {"kind": "servo", "servo": 1, "angle": 90})

        import asyncio

        asyncio.run(exercise())

    def test_device_dispatch_waits_for_the_matching_esp32_acknowledgement(self) -> None:
        """A browser success must mean ESP32 accepted this exact command."""

        async def exercise() -> None:
            import asyncio

            registry = DeviceControlRegistry()
            websocket = CapturingWebSocket()
            session = DeviceSession(
                device_id="dev_001",
                user_id="operator",
                session_id="ses_001",
                conversation_id="conv_001",
            )
            await registry.register(websocket, session)  # type: ignore[arg-type]

            dispatch_and_confirm = getattr(registry, "dispatch_and_confirm", None)
            self.assertIsNotNone(
                dispatch_and_confirm,
                "operator controls must wait for an ESP32 action.result acknowledgement",
            )
            pending = asyncio.create_task(
                dispatch_and_confirm(  # type: ignore[misc]
                    "dev_001", RemoteControlRequest(kind="expression", expression="happy")
                )
            )
            while not websocket.frames:
                await asyncio.sleep(0)
            outbound = parse_control_event(websocket.frames[0])
            self.assertEqual(outbound.type, "operator.control")
            self.assertIsNotNone(outbound.request_id)

            complete_operator_control = getattr(
                registry, "complete_operator_control", None
            )
            self.assertIsNotNone(
                complete_operator_control,
                "the matching ESP32 acknowledgement must complete the pending control",
            )
            complete_operator_control(  # type: ignore[misc]
                session,
                request_id=outbound.request_id,
                status="accepted",
                error_code=None,
            )

            acknowledgement = await asyncio.wait_for(pending, timeout=0.2)
            self.assertEqual(acknowledgement.request_id, outbound.request_id)
            self.assertEqual(acknowledgement.status, "accepted")
            self.assertIsNone(acknowledgement.error_code)

        import asyncio

        asyncio.run(exercise())


if __name__ == "__main__":
    unittest.main()
