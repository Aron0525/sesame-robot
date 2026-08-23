from __future__ import annotations

import asyncio
import unittest

from sesame_voice_gateway.openclaw.sse import ReplyFinal, ReplySentence
from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.pipeline import ConversationContext
from sesame_voice_gateway.providers.base import AsrResult, AudioFormat, VoiceSpec
from sesame_voice_gateway.streaming import StreamAudio, StreamFinished, StreamStart, StreamingConversationPipeline


class _Codec:
    def decode_packet(self, packet: bytes) -> bytes:
        self.last_decoded = packet
        return b"\x01\x00" * 320

    def encode_frame(self, pcm: bytes) -> bytes:
        return b"opus:" + pcm[:4]


class _Asr:
    async def transcribe(self, pcm: bytes, audio_format: AudioFormat) -> AsrResult:
        self.last_pcm = pcm
        self.audio_format = audio_format
        return AsrResult(text="请介绍今天的天气")


class _Agent:
    async def stream_reply(self, **kwargs: object):
        self.kwargs = kwargs
        yield ReplySentence(text="今天天气不错。", sequence=1)
        yield ReplyFinal(expression="happy", sequence=2)


class _Tts:
    def __init__(self) -> None:
        self.allow_second_chunk = asyncio.Event()

    async def stream_synthesize(self, text: str, voice: VoiceSpec):
        self.text = text
        self.voice = voice
        yield b"\x02\x00" * 320
        await self.allow_second_chunk.wait()
        yield b"\x03\x00" * 320


class _ImmediateTts:
    async def stream_synthesize(self, text: str, voice: VoiceSpec):
        self.text = text
        yield b"\x02\x00" * 320


class _CompletingAgent:
    def __init__(self) -> None:
        self.final_read = asyncio.Event()

    async def stream_reply(self, **kwargs: object):
        self.kwargs = kwargs
        yield ReplySentence(text="第一句。", sequence=1)
        yield ReplySentence(text="第二句。", sequence=2)
        self.final_read.set()
        yield ReplyFinal(expression="happy", sequence=3)


class StreamingConversationPipelineTest(unittest.IsolatedAsyncioTestCase):
    async def test_asr_final_starts_a_safe_plan_then_streams_sentence_opus(self) -> None:
        codec = _Codec()
        asr = _Asr()
        agent = _Agent()
        tts = _Tts()
        pipeline = StreamingConversationPipeline(
            codec_factory=lambda: codec,
            asr=asr,
            agent=agent,
            tts=tts,
        )
        context = ConversationContext(
            device_id="device_001",
            user_id="user_001",
            conversation_id="conv_001",
            turn_id="turn_001",
        )

        outputs = pipeline.stream_turn(context=context, opus_packets=[b"uplink-opus"])

        self.assertEqual(await anext(outputs), StreamStart(generation_id=1, expression="neutral"))
        first_audio = await asyncio.wait_for(anext(outputs), timeout=0.1)
        self.assertEqual(type(first_audio), StreamAudio)
        self.assertEqual(first_audio.sentence, "今天天气不错。")
        self.assertEqual(first_audio.opus_packets, (b"opus:\x02\x00\x02\x00",))

        tts.allow_second_chunk.set()
        second_audio = await anext(outputs)
        self.assertEqual(second_audio.opus_packets, (b"opus:\x03\x00\x03\x00",))
        self.assertEqual(await anext(outputs), StreamFinished(expression="happy"))
        self.assertEqual(agent.kwargs["text"], "请介绍今天的天气")
        self.assertEqual(tts.text, "今天天气不错。")

    async def test_records_asr_and_openclaw_text_for_local_debugging(self) -> None:
        observer = ObservabilityStore(max_events=20, expose_debug_content=True)
        pipeline = StreamingConversationPipeline(
            codec_factory=_Codec,
            asr=_Asr(),
            agent=_Agent(),
            tts=_ImmediateTts(),
            observer=observer,
        )
        context = ConversationContext(
            device_id="device_001",
            user_id="user_001",
            conversation_id="conv_001",
            turn_id="turn_001",
        )

        _ = [
            output
            async for output in pipeline.stream_turn(
                context=context, opus_packets=[b"uplink-opus"]
            )
        ]

        events = observer.snapshot()["events"]
        asr = next(event for event in events if event["stage"] == "asr" and event["status"] == "completed")
        openclaw = next(
            event
            for event in events
            if event["stage"] == "openclaw" and event["status"] == "completed"
        )
        self.assertEqual(asr["details"]["transcript"], "请介绍今天的天气")
        self.assertEqual(openclaw["details"]["reply_text"], "今天天气不错。")

    async def test_reads_openclaw_to_final_before_the_audio_consumer_drains(self) -> None:
        observer = ObservabilityStore(max_events=20, expose_debug_content=True)
        agent = _CompletingAgent()
        pipeline = StreamingConversationPipeline(
            codec_factory=_Codec,
            asr=_Asr(),
            agent=agent,
            tts=_Tts(),
            observer=observer,
        )
        context = ConversationContext(
            device_id="device_001",
            user_id="user_001",
            conversation_id="conv_001",
            turn_id="turn_001",
        )

        outputs = pipeline.stream_turn(context=context, opus_packets=[b"uplink-opus"])
        self.assertEqual(await anext(outputs), StreamStart(generation_id=1, expression="neutral"))

        for _ in range(20):
            if agent.final_read.is_set():
                break
            await asyncio.sleep(0)

        self.assertTrue(agent.final_read.is_set())
        openclaw = next(
            event
            for event in observer.snapshot()["events"]
            if event["stage"] == "openclaw" and event["status"] == "completed"
        )
        self.assertEqual(openclaw["details"]["reply_text"], "第一句。第二句。")
        await outputs.aclose()


if __name__ == "__main__":
    unittest.main()
