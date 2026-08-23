from __future__ import annotations

import struct
import unittest

from sesame_voice_gateway.app import DeviceSession, _send_turn_result
from sesame_voice_gateway.audio.opus import OpusCodec
from sesame_voice_gateway.pipeline import ConversationContext, ConversationPipeline, TurnResult
from sesame_voice_gateway.protocol.audio import AudioDirection, AudioFrame, pack_audio_frame, unpack_audio_frame
from sesame_voice_gateway.providers.base import AgentResult, AsrResult, ExpressionSpec, VoiceSpec


def pcm_frame(amplitude: int) -> bytes:
    """Return one non-silent 20-ms, 16-kHz mono S16LE frame."""
    samples = tuple(amplitude if index % 2 == 0 else -amplitude for index in range(320))
    return struct.pack("<320h", *samples)


class CaptureAsr:
    async def transcribe(self, pcm: bytes, audio_format: object) -> AsrResult:
        del audio_format
        if len(pcm) != 640 or not any(pcm):
            raise ValueError("gateway did not receive a decoded microphone frame")
        return AsrResult(text="回环测试")


class ReplyAgent:
    async def reply(self, **_: object) -> AgentResult:
        return AgentResult(
            text="音频回环成功",
            voice=VoiceSpec(),
            expression=ExpressionSpec(name="happy", ttl_ms=1_000),
        )


class ToneTts:
    async def synthesize(self, text: str, voice: VoiceSpec) -> bytes:
        del voice
        if text != "音频回环成功":
            raise ValueError("unexpected TTS text")
        return pcm_frame(4_000)


class FakeWebSocket:
    def __init__(self) -> None:
        self.text_frames: list[str] = []
        self.binary_frames: list[bytes] = []

    async def send_text(self, value: str) -> None:
        self.text_frames.append(value)

    async def send_bytes(self, value: bytes) -> None:
        self.binary_frames.append(value)


class AudioLoopbackTest(unittest.IsolatedAsyncioTestCase):
    async def test_microphone_opus_gateway_tts_opus_and_speaker_pcm_round_trip(self) -> None:
        microphone_pcm = pcm_frame(3_000)
        microphone_packet = OpusCodec().encode_frame(microphone_pcm)
        uplink_wire = pack_audio_frame(
            AudioFrame(
                direction=AudioDirection.UPLINK,
                flags=0,
                stream_id=1,
                generation_id=1,
                sequence=0,
                timestamp_ms=1_000,
                payload=microphone_packet,
            )
        )

        received_uplink = unpack_audio_frame(uplink_wire)
        self.assertEqual(received_uplink.direction, AudioDirection.UPLINK)
        self.assertEqual(received_uplink.stream_id, 1)

        pipeline = ConversationPipeline(
            codec_factory=OpusCodec,
            asr=CaptureAsr(),
            agent=ReplyAgent(),
            tts=ToneTts(),
        )
        result = await pipeline.process_turn(
            context=ConversationContext(
                device_id="device_001",
                user_id="user_001",
                conversation_id="conversation_001",
                turn_id="turn_001",
            ),
            opus_packets=[received_uplink.payload],
        )
        self.assertIsInstance(result, TurnResult)

        websocket = FakeWebSocket()
        session = DeviceSession(
            device_id="device_001",
            user_id="user_001",
            session_id="session_001",
            conversation_id="conversation_001",
            turn_id="turn_001",
        )
        await _send_turn_result(websocket, session, result)

        downlink_frames = [unpack_audio_frame(frame) for frame in websocket.binary_frames]
        self.assertEqual(len(downlink_frames), 1)
        self.assertEqual(downlink_frames[0].direction, AudioDirection.DOWNLINK)
        self.assertEqual(downlink_frames[0].stream_id, 2)
        self.assertEqual(downlink_frames[0].generation_id, result.generation_id)
        self.assertEqual(downlink_frames[0].sequence, 0)

        speaker_pcm = OpusCodec().decode_packet(downlink_frames[0].payload)
        self.assertEqual(len(speaker_pcm), 640)
        self.assertNotEqual(speaker_pcm, b"\x00" * 640)


if __name__ == "__main__":
    unittest.main()
