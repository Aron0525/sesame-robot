from __future__ import annotations

import os
import unittest
from unittest.mock import patch

from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.pipeline import (
    ConversationContext,
    ConversationPipeline,
    SilentDiscard,
)
from sesame_voice_gateway.privacy import PrivacyPolicyViolation, validate_remote_pcm
from sesame_voice_gateway.providers.base import (
    AgentToolCall,
    AgentResult,
    AsrResult,
    ExpressionSpec,
    VoiceSpec,
)
from sesame_voice_gateway.tools.web_search import WebSearchRequest, WebSearchResult, WebSearchSource
from sesame_voice_gateway.providers.dashscope import DashScopeAudioClient


class FakeCodec:
    def __init__(self, identifier: int = 0) -> None:
        self.identifier = identifier

    def decode_packet(self, packet: bytes) -> bytes:
        if packet != b"uplink-opus":
            raise ValueError("unexpected test packet")
        return b"\x00" * 640

    def encode_frame(self, pcm: bytes) -> bytes:
        if len(pcm) != 640:
            raise ValueError("unexpected PCM frame size")
        return b"downlink-opus"


class FakeAsr:
    async def transcribe(self, pcm: bytes, audio_format: object) -> AsrResult:
        self.pcm_bytes = len(pcm)
        return AsrResult(text="合成测试请求")


class SilentAsr:
    async def transcribe(self, pcm: bytes, audio_format: object) -> AsrResult:
        del pcm, audio_format
        return AsrResult(text="")


class FillerAsr:
    async def transcribe(self, pcm: bytes, audio_format: object) -> AsrResult:
        del pcm, audio_format
        return AsrResult(text="嗯嗯，那个就是")


class FakeAgent:
    async def reply(self, **_: object) -> AgentResult:
        return AgentResult(
            text="合成测试回复",
            voice=VoiceSpec(),
            expression=ExpressionSpec(name="happy", ttl_ms=1_000),
        )


class SearchThenAnswerAgent:
    def __init__(self) -> None:
        self.prompts: list[str] = []

    async def reply(self, *, text: str, allow_web_search: bool = False, **_: object) -> AgentResult | AgentToolCall:
        self.prompts.append(text)
        if allow_web_search:
            return AgentToolCall(name="web_search", arguments={"query": "深圳明天天气"})
        return AgentResult(
            text="深圳明天多云，气温 25°C。",
            voice=VoiceSpec(),
            expression=ExpressionSpec(name="thinking", ttl_ms=1_000),
        )


class FakeWebSearch:
    def __init__(self) -> None:
        self.requests: list[WebSearchRequest] = []

    async def search(self, request: WebSearchRequest) -> WebSearchResult:
        self.requests.append(request)
        return WebSearchResult(
            summary="深圳明天多云，气温 25°C。",
            sources=(
                WebSearchSource(
                    title="深圳天气",
                    url="https://weather.example.test/shenzhen",
                    snippet="多云",
                    published_at="2026-08-03",
                ),
            ),
        )


class FakeTts:
    async def synthesize(self, text: str, voice: VoiceSpec) -> bytes:
        if text not in {"合成测试回复", "深圳明天多云，气温 25°C。"}:
            raise ValueError("unexpected test text")
        return b"\x00" * 640


class RecordingTts:
    def __init__(self) -> None:
        self.texts: list[str] = []

    async def synthesize(self, text: str, voice: VoiceSpec) -> bytes:
        del voice
        self.texts.append(text)
        return b"\x00" * 640


class FailIfCalledAgent:
    async def reply(self, **_: object) -> AgentResult:
        raise AssertionError("OpenClaw must not receive an empty ASR result")


class FakeObserver:
    def __init__(self) -> None:
        self.events: list[tuple[str, str, dict[str, object]]] = []

    def record_stage(
        self,
        *,
        device_id: str,
        turn_id: str,
        stage: str,
        status: str,
        elapsed_ms: int | None = None,
        details: dict[str, object] | None = None,
    ) -> None:
        del device_id, turn_id, elapsed_ms
        self.events.append((stage, status, details or {}))


class PrivacyAndPipelineTest(unittest.IsolatedAsyncioTestCase):
    def test_remote_speech_requires_explicit_consent(self) -> None:
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "ALLOW_REMOTE_SPEECH"):
                Settings(
                    _env_file=None,
                    dashscope_api_key="test-key",
                    openclaw_token="test-token",
                    openclaw_session_key_secret="test-secret",
                    device_tokens={"device": "token"},
                    device_users={"device": "user"},
                )

    def test_remote_audio_has_a_30_second_limit(self) -> None:
        with self.assertRaises(PrivacyPolicyViolation):
            validate_remote_pcm(b"\x00" * (16_000 * 2 * 30 + 1))

    def test_dashscope_urls_must_use_tls(self) -> None:
        with self.assertRaises(PrivacyPolicyViolation):
            DashScopeAudioClient(
                api_key="test-key",
                http_base_url="http://example.test/api/v1",
                websocket_base_url="wss://example.test/api-ws/v1/inference",
            )

    def test_serial_monitor_requires_a_registered_device(self) -> None:
        with self.assertRaisesRegex(ValueError, "SERIAL_MONITOR_DEVICE_ID"):
            Settings(
                _env_file=None,
                device_tokens={"device": "token"},
                device_users={"device": "user"},
                allow_remote_speech=True,
                dashscope_api_key="test-key",
                openclaw_token="test-token",
                openclaw_session_key_secret="test-secret",
                serial_monitor_enabled=True,
                serial_monitor_device_id="missing-device",
            )

    def test_dashboard_text_is_visible_by_default(self) -> None:
        settings = Settings(
            _env_file=None,
            device_tokens={"device": "token"},
            device_users={"device": "user"},
            allow_remote_speech=True,
            dashscope_api_key="test-key",
            openclaw_token="test-token",
            openclaw_session_key_secret="test-secret",
        )
        store = ObservabilityStore(expose_debug_content=settings.dashboard_debug_content)
        store.record_stage(
            device_id="device",
            turn_id="turn",
            stage="asr",
            status="completed",
            details={"transcript": "请站立"},
        )
        store.record_stage(
            device_id="device",
            turn_id="turn",
            stage="openclaw",
            status="completed",
            details={"reply_text": "好的，正在站立。"},
        )

        details = [event["details"] for event in store.snapshot()["events"]]

        self.assertTrue(settings.dashboard_debug_content)
        self.assertEqual(details[0]["transcript"], "请站立")
        self.assertEqual(details[1]["reply_text"], "好的，正在站立。")

    async def test_pipeline_completes_a_synthetic_turn_without_external_data(self) -> None:
        asr = FakeAsr()
        pipeline = ConversationPipeline(
            codec_factory=lambda: FakeCodec(),
            asr=asr,
            agent=FakeAgent(),
            tts=FakeTts(),
        )
        result = await pipeline.process_turn(
            context=ConversationContext(
                device_id="device",
                user_id="user",
                conversation_id="conversation",
                turn_id="turn",
            ),
            opus_packets=[b"uplink-opus"],
        )
        self.assertEqual(asr.pcm_bytes, 640)
        self.assertEqual(result.transcript.text, "合成测试请求")
        self.assertEqual(result.opus_packets, (b"downlink-opus",))
        self.assertEqual(result.generation_id, 1)

    async def test_pipeline_executes_one_web_search_then_generates_the_final_reply(self) -> None:
        agent = SearchThenAnswerAgent()
        search = FakeWebSearch()
        pipeline = ConversationPipeline(
            codec_factory=FakeCodec,
            asr=FakeAsr(),
            agent=agent,
            tts=FakeTts(),
            web_search=search,
        )

        result = await pipeline.process_turn(
            context=ConversationContext(
                device_id="device",
                user_id="user",
                conversation_id="conversation",
                turn_id="turn",
            ),
            opus_packets=[b"uplink-opus"],
        )

        self.assertEqual(search.requests, [WebSearchRequest(query="深圳明天天气")])
        self.assertEqual(result.agent.text, "深圳明天多云，气温 25°C。")
        self.assertIn("UNTRUSTED_WEB_SEARCH_RESULT", agent.prompts[1])

    async def test_pipeline_silently_discards_when_asr_returns_no_speech(self) -> None:
        tts = RecordingTts()
        pipeline = ConversationPipeline(
            codec_factory=FakeCodec,
            asr=SilentAsr(),
            agent=FailIfCalledAgent(),
            tts=tts,
        )

        result = await pipeline.process_turn(
            context=ConversationContext(
                device_id="device",
                user_id="user",
                conversation_id="conversation",
                turn_id="turn",
            ),
            opus_packets=[b"uplink-opus"],
        )

        self.assertIsInstance(result, SilentDiscard)
        self.assertEqual(result.transcript.text, "")
        self.assertEqual(result.reason, "blank")
        self.assertEqual(tts.texts, [])

    async def test_pipeline_silently_discards_filler_only_asr_text(self) -> None:
        tts = RecordingTts()
        pipeline = ConversationPipeline(
            codec_factory=FakeCodec,
            asr=FillerAsr(),
            agent=FailIfCalledAgent(),
            tts=tts,
        )

        result = await pipeline.process_turn(
            context=ConversationContext(
                device_id="device",
                user_id="user",
                conversation_id="conversation",
                turn_id="turn",
            ),
            opus_packets=[b"uplink-opus"],
        )

        self.assertIsInstance(result, SilentDiscard)
        self.assertEqual(result.transcript.text, "嗯嗯，那个就是")
        self.assertEqual(result.reason, "filler_only")
        self.assertEqual(tts.texts, [])

    async def test_each_turn_gets_a_fresh_opus_codec(self) -> None:
        created_codecs: list[FakeCodec] = []

        def create_codec() -> FakeCodec:
            codec = FakeCodec(identifier=len(created_codecs) + 1)
            created_codecs.append(codec)
            return codec

        pipeline = ConversationPipeline(
            codec_factory=create_codec,
            asr=FakeAsr(),
            agent=FakeAgent(),
            tts=FakeTts(),
        )
        context = ConversationContext(
            device_id="device",
            user_id="user",
            conversation_id="conversation",
            turn_id="turn",
        )

        await pipeline.process_turn(context=context, opus_packets=[b"uplink-opus"])
        await pipeline.process_turn(context=context, opus_packets=[b"uplink-opus"])

        self.assertEqual(len(created_codecs), 2)
        self.assertIsNot(created_codecs[0], created_codecs[1])

    async def test_pipeline_emits_stage_events_for_the_dashboard(self) -> None:
        observer = FakeObserver()
        pipeline = ConversationPipeline(
            codec_factory=FakeCodec,
            asr=FakeAsr(),
            agent=FakeAgent(),
            tts=FakeTts(),
            observer=observer,
        )

        await pipeline.process_turn(
            context=ConversationContext(
                device_id="device",
                user_id="user",
                conversation_id="conversation",
                turn_id="turn",
            ),
            opus_packets=[b"uplink-opus"],
        )

        self.assertEqual(
            [(stage, status) for stage, status, _ in observer.events],
            [
                ("opus.decode", "started"),
                ("opus.decode", "completed"),
                ("asr", "started"),
                ("asr", "completed"),
                ("openclaw", "started"),
                ("openclaw", "completed"),
                ("tts.synthesis", "started"),
                ("tts.synthesis", "completed"),
                ("opus.encode", "started"),
                ("opus.encode", "completed"),
            ],
        )
        asr_completed = next(
            details
            for stage, status, details in observer.events
            if stage == "asr" and status == "completed"
        )
        self.assertEqual(asr_completed["transcript_chars"], 6)


if __name__ == "__main__":
    unittest.main()
