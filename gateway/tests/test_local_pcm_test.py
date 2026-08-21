from __future__ import annotations

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from fastapi.testclient import TestClient

from sesame_voice_gateway.app import (
    DeviceControlRegistry,
    DeviceSession,
    _build_local_pcm_test_result,
    create_app,
)
from sesame_voice_gateway.audio.opus import OpusCodec
from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.local_pcm_test import LocalPcmTestError, LocalPcmTestQueue
from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.protocol.audio import unpack_audio_frame
from sesame_voice_gateway.protocol.control import parse_control_event


class _GenerationSource:
    async def next_generation_id(self) -> int:
        return 7


class _CapturingWebSocket:
    def __init__(self) -> None:
        self.text_frames: list[str] = []
        self.binary_frames: list[bytes] = []

    async def send_text(self, value: str) -> None:
        self.text_frames.append(value)

    async def send_bytes(self, value: bytes) -> None:
        self.binary_frames.append(value)


class _FixturePlaybackControls:
    def __init__(self) -> None:
        self.calls: list[tuple[str, bytes]] = []

    async def play_local_pcm_test(
        self,
        device_id: str,
        pcm: bytes,
        pipeline: object,
        observability: ObservabilityStore,
    ) -> tuple[int, int]:
        self.calls.append((device_id, pcm))
        return 9, len(pcm) // 640


class LocalPcmTestQueueTest(unittest.IsolatedAsyncioTestCase):
    async def test_armed_pcm_is_returned_once_for_its_device(self) -> None:
        queue = LocalPcmTestQueue()
        pcm = b"\x00\x00" * 320 * 3

        frame_count = await queue.arm("device_001", pcm)

        self.assertEqual(frame_count, 3)
        self.assertEqual(await queue.consume("device_001"), pcm)
        self.assertIsNone(await queue.consume("device_001"))

    async def test_rejects_empty_or_partial_pcm_frames(self) -> None:
        queue = LocalPcmTestQueue()

        with self.assertRaisesRegex(LocalPcmTestError, "complete"):
            await queue.arm("device_001", b"")
        with self.assertRaisesRegex(LocalPcmTestError, "complete"):
            await queue.arm("device_001", b"\x00" * 641)

    async def test_rejects_pcm_longer_than_twenty_seconds(self) -> None:
        queue = LocalPcmTestQueue()
        pcm = b"\x00\x00" * 320 * 1_001

        with self.assertRaisesRegex(LocalPcmTestError, "20 seconds"):
            await queue.arm("device_001", pcm)

    async def test_pcm_is_encoded_as_one_opus_packet_per_20_ms_frame(self) -> None:
        pcm = b"\x00\x00" * 320 * 2

        result = await _build_local_pcm_test_result(_GenerationSource(), pcm)  # type: ignore[arg-type]

        self.assertEqual(result.generation_id, 7)
        self.assertEqual(len(result.opus_packets), 2)
        self.assertTrue(all(result.opus_packets))
        codec = OpusCodec()
        self.assertEqual(len(codec.decode_packet(result.opus_packets[0])), 640)

    async def test_direct_play_uses_the_normal_wss_downlink_sequence(self) -> None:
        registry = DeviceControlRegistry()
        websocket = _CapturingWebSocket()
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
        )
        await registry.register(websocket, session)  # type: ignore[arg-type]

        generation_id, packet_count = await registry.play_local_pcm_test(
            "device_001",
            b"\x00\x00" * 320 * 2,
            _GenerationSource(),  # type: ignore[arg-type]
            ObservabilityStore(max_events=20),
        )

        controls = [parse_control_event(frame) for frame in websocket.text_frames]
        self.assertEqual(generation_id, 7)
        self.assertEqual(packet_count, 2)
        self.assertEqual(
            [event.type for event in controls],
            ["response.plan", "tts.start", "tts.stop"],
        )
        self.assertTrue(controls[0].turn_id.startswith("test_"))
        self.assertEqual({event.turn_id for event in controls}, {controls[0].turn_id})
        self.assertEqual(len(websocket.binary_frames), 2)
        self.assertEqual(
            [unpack_audio_frame(frame).sequence for frame in websocket.binary_frames],
            [0, 1],
        )
        self.assertTrue(
            all(unpack_audio_frame(frame).generation_id == generation_id for frame in websocket.binary_frames)
        )

    async def test_pause_and_resume_keep_the_active_turn_binding(self) -> None:
        registry = DeviceControlRegistry()
        websocket = _CapturingWebSocket()
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="ses_001",
            conversation_id="conv_001",
            active_generation=7,
            playback_stats_generation=7,
            playback_stats_turn_id="test_001",
        )
        await registry.register(websocket, session)  # type: ignore[arg-type]

        await registry.set_playback_paused("device_001", paused=True)
        self.assertTrue(session.playback_paused)
        await registry.set_playback_paused("device_001", paused=False)
        self.assertFalse(session.playback_paused)

        controls = [parse_control_event(frame) for frame in websocket.text_frames]
        self.assertEqual([event.type for event in controls], ["tts.pause", "tts.resume"])
        self.assertEqual([event.turn_id for event in controls], ["test_001", "test_001"])
        self.assertEqual([event.payload for event in controls], [{"generation_id": 7}] * 2)


class LocalPcmTestRouteTest(unittest.TestCase):
    def test_route_arms_complete_pcm_only_for_a_local_client(self) -> None:
        settings = Settings(
            _env_file=None,
            device_tokens={"device": "token"},
            device_users={"device": "user"},
            allow_remote_speech=True,
            dashscope_api_key="test-key",
            openclaw_token="test-token",
            openclaw_session_key_secret="test-secret",
        )
        app = create_app(
            settings,
            pipeline=object(),  # type: ignore[arg-type]
            observability=ObservabilityStore(max_events=20),
        )

        with TestClient(app, client=("127.0.0.1", 4321)) as client:
            response = client.put(
                "/api/local-pcm-test/device",
                content=b"\x00\x00" * 320,
                headers={"content-type": "application/octet-stream"},
            )

        self.assertEqual(response.status_code, 202)
        self.assertEqual(
            response.json(),
            {
                "status": "armed",
                "device_id": "device",
                "frame_count": 1,
                "duration_ms": 20,
            },
        )

    def test_fixed_beijing_fixture_plays_without_browser_file_upload(self) -> None:
        with TemporaryDirectory() as directory:
            fixture = Path(directory) / "beijing_welcome.pcm"
            pcm = b"\x00\x00" * 320
            fixture.write_bytes(pcm)
            settings = Settings(
                _env_file=None,
                device_tokens={"device": "token"},
                device_users={"device": "user"},
                allow_remote_speech=True,
                dashscope_api_key="test-key",
                openclaw_token="test-token",
                openclaw_session_key_secret="test-secret",
                local_pcm_test_fixture_path=fixture,
            )
            controls = _FixturePlaybackControls()
            app = create_app(
                settings,
                pipeline=object(),  # type: ignore[arg-type]
                observability=ObservabilityStore(max_events=20),
                device_controls=controls,  # type: ignore[arg-type]
            )

            with TestClient(app, client=("127.0.0.1", 4321)) as client:
                response = client.post("/api/local-pcm-test/device/beijing-welcome/play")

        self.assertEqual(response.status_code, 202)
        self.assertEqual(
            response.json(),
            {
                "status": "playing",
                "fixture": "beijing-welcome",
                "device_id": "device",
                "generation_id": 9,
                "packet_count": 1,
                "duration_ms": 20,
            },
        )
        self.assertEqual(controls.calls, [("device", pcm)])
