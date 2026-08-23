from __future__ import annotations

import json
import tempfile
import unittest
import wave
from pathlib import Path

from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.pipeline import ConversationContext, ConversationPipeline
from sesame_voice_gateway.providers.base import AudioFormat
from sesame_voice_gateway.recordings import TestRecordingStore, create_test_recording_store


class TestRecordingSettingsTest(unittest.TestCase):
    def test_test_recordings_are_disabled_by_default(self) -> None:
        settings = Settings(
            _env_file=None,
            device_tokens={"device": "token"},
            device_users={"device": "user"},
            allow_remote_speech=True,
            dashscope_api_key="test-key",
            openclaw_token="test-token",
            openclaw_session_key_secret="test-secret",
        )

        self.assertIs(settings.model_dump().get("save_test_recordings"), False)


class TestRecordingStoreTest(unittest.TestCase):
    def test_factory_creates_a_store_only_when_test_retention_is_enabled(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory)

            self.assertIsNone(
                create_test_recording_store(
                    enabled=False,
                    output_directory=output_directory,
                    limit=10,
                )
            )
            self.assertIsInstance(
                create_test_recording_store(
                    enabled=True,
                    output_directory=output_directory,
                    limit=10,
                ),
                TestRecordingStore,
            )

    def test_saves_pcm_as_wav_and_appends_a_manifest_entry(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory)
            store = TestRecordingStore(output_directory=output_directory, limit=10)
            pcm = b"\x01\x00" * 320

            artifact = store.save(
                device_id="sesame-v3-001",
                turn_id="turn-001",
                pcm=pcm,
                audio_format=AudioFormat(),
            )

            self.assertIsNotNone(artifact)
            assert artifact is not None
            self.assertEqual(artifact.index, 1)
            self.assertEqual(artifact.path.name, "001_turn-001.wav")
            with wave.open(str(artifact.path), "rb") as recorded:
                self.assertEqual(recorded.getnchannels(), 1)
                self.assertEqual(recorded.getframerate(), 16_000)
                self.assertEqual(recorded.getsampwidth(), 2)
                self.assertEqual(recorded.readframes(recorded.getnframes()), pcm)

            entries = [
                json.loads(line)
                for line in (output_directory / "manifest.jsonl").read_text(encoding="utf-8").splitlines()
            ]
            self.assertEqual(
                entries,
                [
                    {
                        "device_id": "sesame-v3-001",
                        "duration_ms": 20,
                        "file": "001_turn-001.wav",
                        "index": 1,
                        "turn_id": "turn-001",
                    }
                ],
            )

    def test_discards_the_oldest_wav_after_the_configured_limit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory)
            store = TestRecordingStore(output_directory=output_directory, limit=1)
            pcm = b"\x00\x00" * 320

            first = store.save(
                device_id="device",
                turn_id="turn-001",
                pcm=pcm,
                audio_format=AudioFormat(),
            )
            second = store.save(
                device_id="device",
                turn_id="turn-002",
                pcm=pcm,
                audio_format=AudioFormat(),
            )

            self.assertIsNotNone(first)
            self.assertIsNotNone(second)
            assert second is not None
            self.assertEqual(second.index, 2)
            self.assertEqual(
                sorted(path.name for path in output_directory.glob("*.wav")),
                ["002_turn-002.wav"],
            )


class _Codec:
    def decode_packet(self, packet: bytes) -> bytes:
        if packet != b"uplink-opus":
            raise ValueError("unexpected packet")
        return b"\x01\x00" * 320

    def encode_frame(self, pcm: bytes) -> bytes:
        if len(pcm) != 640:
            raise ValueError("unexpected PCM frame")
        return b"downlink-opus"


class _Asr:
    async def transcribe(self, pcm: bytes, audio_format: AudioFormat) -> object:
        del pcm, audio_format
        from sesame_voice_gateway.providers.base import AsrResult

        return AsrResult(text="测试")


class _Agent:
    async def reply(self, **_: object) -> object:
        from sesame_voice_gateway.providers.base import AgentResult

        return AgentResult(text="收到")


class _Tts:
    async def synthesize(self, text: str, voice: object) -> bytes:
        del text, voice
        return b"\x00" * 640


class TestRecordingPipelineTest(unittest.IsolatedAsyncioTestCase):
    async def test_pipeline_writes_the_decoded_uplink_pcm_when_enabled(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_directory = Path(temporary_directory)
            pipeline = ConversationPipeline(
                codec_factory=_Codec,
                asr=_Asr(),
                agent=_Agent(),
                tts=_Tts(),
                recording_store=TestRecordingStore(output_directory=output_directory, limit=10),
            )

            await pipeline.process_turn(
                context=ConversationContext(
                    device_id="device",
                    user_id="user",
                    conversation_id="conversation",
                    turn_id="turn-001",
                ),
                opus_packets=[b"uplink-opus"],
            )

            with wave.open(str(output_directory / "001_turn-001.wav"), "rb") as recorded:
                self.assertEqual(recorded.readframes(recorded.getnframes()), b"\x01\x00" * 320)


if __name__ == "__main__":
    unittest.main()
