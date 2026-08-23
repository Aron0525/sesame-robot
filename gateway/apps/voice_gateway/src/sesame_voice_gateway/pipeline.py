from __future__ import annotations

import asyncio
import logging
import struct
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Protocol

from sesame_voice_gateway.policy import validate_agent_result, validate_asr_result
from sesame_voice_gateway.privacy import validate_remote_pcm, validate_remote_tts_text
from sesame_voice_gateway.providers.base import (
    AgentToolCall,
    AgentProvider,
    AgentResult,
    AsrProvider,
    AsrResult,
    AudioFormat,
    NoSpeechDetected,
    TtsProvider,
)
from sesame_voice_gateway.recordings import TestRecordingStore
from sesame_voice_gateway.text_validity import TranscriptDisposition, classify_transcript
from sesame_voice_gateway.tools.web_search import (
    WebSearchPolicyError,
    WebSearchProvider,
    WebSearchUnavailableError,
    format_untrusted_web_search_result,
    validate_web_search_request,
)

MAX_UPLINK_PACKETS = 1_500
logger = logging.getLogger(__name__)


def pcm_signal_metrics(pcm: bytes, *, sample_rate_hz: int = 16_000) -> dict[str, int]:
    """Return aggregate level data without retaining or exposing speech content."""
    if sample_rate_hz <= 0:
        raise ValueError("sample_rate_hz must be positive")
    if len(pcm) % 2:
        raise ValueError("PCM audio must contain complete S16LE samples")

    sample_count = len(pcm) // 2
    if sample_count == 0:
        return {
            "pcm_duration_ms": 0,
            "sample_count": 0,
            "sample_peak_abs": 0,
            "sample_mean_abs": 0,
            "nonzero_sample_pct": 0,
            "near_clip_sample_pct": 0,
        }

    total_abs = 0
    peak_abs = 0
    nonzero_count = 0
    near_clip_count = 0
    for (sample,) in struct.iter_unpack("<h", pcm):
        magnitude = abs(sample)
        total_abs += magnitude
        peak_abs = max(peak_abs, magnitude)
        nonzero_count += magnitude != 0
        near_clip_count += magnitude >= 32_000

    def percent(count: int) -> int:
        return (count * 100 + sample_count // 2) // sample_count

    return {
        "pcm_duration_ms": sample_count * 1_000 // sample_rate_hz,
        "sample_count": sample_count,
        "sample_peak_abs": peak_abs,
        "sample_mean_abs": total_abs // sample_count,
        "nonzero_sample_pct": percent(nonzero_count),
        "near_clip_sample_pct": percent(near_clip_count),
    }


class TurnCodec(Protocol):
    def decode_packet(self, packet: bytes) -> bytes: ...

    def encode_frame(self, pcm: bytes) -> bytes: ...


class TurnObserver(Protocol):
    """Receives metadata-only progress notifications for one voice turn."""

    def record_stage(
        self,
        *,
        device_id: str,
        turn_id: str,
        stage: str,
        status: str,
        elapsed_ms: int | None = None,
        details: dict[str, object] | None = None,
    ) -> None: ...


@dataclass(frozen=True, slots=True)
class ConversationContext:
    device_id: str
    user_id: str
    conversation_id: str
    turn_id: str
    capture_trigger: str = "unknown"


@dataclass(frozen=True, slots=True)
class TurnResult:
    transcript: AsrResult
    agent: AgentResult
    generation_id: int
    opus_packets: tuple[bytes, ...]


@dataclass(frozen=True, slots=True)
class SilentDiscard:
    """A completed turn that must leave the device idle without a reply."""

    transcript: AsrResult
    reason: str


TurnOutcome = TurnResult | SilentDiscard


class ConversationPipeline:
    def __init__(
        self,
        *,
        codec_factory: Callable[[], TurnCodec],
        asr: AsrProvider,
        agent: AgentProvider,
        tts: TtsProvider,
        audio_format: AudioFormat | None = None,
        observer: TurnObserver | None = None,
        web_search: WebSearchProvider | None = None,
        recording_store: TestRecordingStore | None = None,
        manual_test_recordings_only: bool = False,
    ) -> None:
        self._codec_factory = codec_factory
        self._asr = asr
        self._agent = agent
        self._tts = tts
        self._audio_format = audio_format or AudioFormat()
        self._observer = observer
        self._web_search = web_search
        self._recording_store = recording_store
        self._manual_test_recordings_only = manual_test_recordings_only
        self._generation_id = 0
        self._generation_lock = asyncio.Lock()

    async def process_turn(
        self,
        *,
        context: ConversationContext,
        opus_packets: list[bytes],
    ) -> TurnOutcome:
        if not opus_packets:
            raise ValueError("a turn requires at least one Opus packet")
        if len(opus_packets) > MAX_UPLINK_PACKETS:
            raise ValueError("turn exceeds maximum audio duration")

        codec = self._codec_factory()
        decode_started_at = time.perf_counter()
        self._record_stage(
            context=context,
            stage="opus.decode",
            status="started",
            details={"packet_count": len(opus_packets)},
        )
        try:
            pcm = b"".join(codec.decode_packet(packet) for packet in opus_packets)
        except Exception as exc:
            self._record_failure(
                context=context,
                stage="opus.decode",
                started_at=decode_started_at,
                exc=exc,
            )
            raise
        self._record_stage(
            context=context,
            stage="opus.decode",
            status="completed",
            elapsed_ms=self._elapsed_ms(decode_started_at),
            details={"packet_count": len(opus_packets), "pcm_bytes": len(pcm)},
        )
        return await self._process_pcm_turn(context=context, pcm=pcm, codec=codec)

    async def process_pcm_turn(
        self,
        *,
        context: ConversationContext,
        pcm: bytes,
    ) -> TurnOutcome:
        return await self._process_pcm_turn(
            context=context,
            pcm=pcm,
            codec=self._codec_factory(),
        )

    async def _process_pcm_turn(
        self,
        *,
        context: ConversationContext,
        pcm: bytes,
        codec: TurnCodec,
    ) -> TurnOutcome:
        if not pcm:
            raise ValueError("a turn requires PCM audio")
        if len(pcm) % self._audio_format.sample_width_bytes:
            raise ValueError("PCM audio must contain complete samples")
        validate_remote_pcm(pcm)
        asr_started_at = time.perf_counter()
        self._record_stage(
            context=context,
            stage="asr",
            status="started",
            details={"pcm_bytes": len(pcm)},
        )
        provider_reported_no_speech = False
        try:
            transcript = await self._asr.transcribe(pcm, self._audio_format)
        except NoSpeechDetected:
            transcript = AsrResult(text="")
            provider_reported_no_speech = True
        except Exception as exc:
            self._record_failure(context=context, stage="asr", started_at=asr_started_at, exc=exc)
            raise
        disposition = classify_transcript(transcript.text)
        if disposition == TranscriptDisposition.VALID:
            transcript = validate_asr_result(transcript)

        await self._save_test_recording(
            context=context,
            pcm=pcm,
            asr_text=transcript.text,
        )

        if disposition != TranscriptDisposition.VALID:
            reason = (
                "asr_no_speech"
                if provider_reported_no_speech
                else disposition.value
            )
            self._record_stage(
                context=context,
                stage="asr",
                status="discarded",
                elapsed_ms=self._elapsed_ms(asr_started_at),
                details={
                    "transcript": transcript.text,
                    "transcript_chars": len(transcript.text),
                    "reason": reason,
                },
            )
            self._record_stage(
                context=context,
                stage="openclaw",
                status="skipped",
                details={"reason": reason},
            )
            self._record_stage(
                context=context,
                stage="tts.synthesis",
                status="skipped",
                details={"reason": reason},
            )
            return SilentDiscard(transcript=transcript, reason=reason)

        self._record_stage(
            context=context,
            stage="asr",
            status="completed",
            elapsed_ms=self._elapsed_ms(asr_started_at),
            details={"transcript": transcript.text, "transcript_chars": len(transcript.text)},
        )

        agent_started_at = time.perf_counter()
        self._record_stage(context=context, stage="openclaw", status="started")
        try:
            agent_reply = await self._agent.reply(
                text=transcript.text,
                device_id=context.device_id,
                user_id=context.user_id,
                conversation_id=context.conversation_id,
                turn_id=context.turn_id,
                allow_web_search=self._web_search is not None,
            )
            agent_result = await self._resolve_agent_tool_call(
                agent_reply=agent_reply,
                original_text=transcript.text,
                context=context,
            )
        except Exception as exc:
            self._record_failure(
                context=context,
                stage="openclaw",
                started_at=agent_started_at,
                exc=exc,
            )
            raise
        self._record_stage(
            context=context,
            stage="openclaw",
            status="completed",
            elapsed_ms=self._elapsed_ms(agent_started_at),
            details={
                "reply_text": agent_result.text,
                "reply_chars": len(agent_result.text),
                "expression": agent_result.expression.name,
                "actions": [action.name for action in agent_result.actions],
            },
        )

        validate_agent_result(agent_result)
        validate_remote_tts_text(agent_result.text)

        tts_started_at = time.perf_counter()
        self._record_stage(
            context=context,
            stage="tts.synthesis",
            status="started",
            details={"input_chars": len(agent_result.text), "style": agent_result.voice.style},
        )
        try:
            tts_pcm = await self._tts.synthesize(agent_result.text, agent_result.voice)
        except Exception as exc:
            self._record_failure(
                context=context,
                stage="tts.synthesis",
                started_at=tts_started_at,
                exc=exc,
            )
            raise
        frame_size = self._audio_format.pcm_bytes_per_frame
        if not tts_pcm or len(tts_pcm) % frame_size:
            raise ValueError(f"TTS PCM must contain complete {frame_size}-byte frames")
        self._record_stage(
            context=context,
            stage="tts.synthesis",
            status="completed",
            elapsed_ms=self._elapsed_ms(tts_started_at),
            details={"pcm_bytes": len(tts_pcm), "frame_count": len(tts_pcm) // frame_size},
        )

        encode_started_at = time.perf_counter()
        self._record_stage(context=context, stage="opus.encode", status="started")
        try:
            opus_output = tuple(
                codec.encode_frame(tts_pcm[offset : offset + frame_size])
                for offset in range(0, len(tts_pcm), frame_size)
            )
        except Exception as exc:
            self._record_failure(
                context=context,
                stage="opus.encode",
                started_at=encode_started_at,
                exc=exc,
            )
            raise
        self._record_stage(
            context=context,
            stage="opus.encode",
            status="completed",
            elapsed_ms=self._elapsed_ms(encode_started_at),
            details={"packet_count": len(opus_output)},
        )
        generation_id = await self.next_generation_id()
        return TurnResult(
            transcript=transcript,
            agent=agent_result,
            generation_id=generation_id,
            opus_packets=opus_output,
        )

    async def _save_test_recording(
        self,
        *,
        context: ConversationContext,
        pcm: bytes,
        asr_text: str,
    ) -> None:
        if self._recording_store is None:
            return
        if self._manual_test_recordings_only and context.capture_trigger != "manual":
            self._record_stage(
                context=context,
                stage="test.recording",
                status="skipped",
                details={"trigger": context.capture_trigger},
            )
            return
        try:
            artifact = await asyncio.to_thread(
                self._recording_store.save,
                device_id=context.device_id,
                turn_id=context.turn_id,
                pcm=pcm,
                audio_format=self._audio_format,
                asr_text=asr_text,
            )
        except Exception as exc:
            logger.warning(
                "test_recording_save_failed device_id=%s turn_id=%s error_type=%s",
                context.device_id,
                context.turn_id,
                type(exc).__name__,
            )
            self._record_stage(
                context=context,
                stage="test.recording",
                status="failed",
                details={"error_type": type(exc).__name__},
            )
            return
        if artifact is None:
            self._record_stage(
                context=context,
                stage="test.recording",
                status="limit_reached",
            )
            return
        self._record_stage(
            context=context,
            stage="test.recording",
            status="completed",
            details={
                "index": artifact.index,
                "path": str(artifact.path),
                "duration_ms": artifact.duration_ms,
            },
        )

    async def _resolve_agent_tool_call(
        self,
        *,
        agent_reply: AgentResult | AgentToolCall,
        original_text: str,
        context: ConversationContext,
    ) -> AgentResult:
        if isinstance(agent_reply, AgentResult):
            return agent_reply
        if self._web_search is None or agent_reply.name != "web_search":
            raise PolicyViolation("agent requested an unavailable tool")
        try:
            request = validate_web_search_request(agent_reply.arguments)
        except WebSearchPolicyError as exc:
            raise PolicyViolation("agent requested invalid web-search arguments") from exc

        started_at = time.perf_counter()
        self._record_stage(context=context, stage="web_search", status="started")
        try:
            result = await self._web_search.search(request)
        except WebSearchUnavailableError as exc:
            self._record_failure(
                context=context,
                stage="web_search",
                started_at=started_at,
                exc=exc,
            )
            raise
        self._record_stage(
            context=context,
            stage="web_search",
            status="completed",
            elapsed_ms=self._elapsed_ms(started_at),
            details={"source_count": len(result.sources)},
        )
        final_reply = await self._agent.reply(
            text=(
                f"Original user request: {original_text}\n\n"
                f"{format_untrusted_web_search_result(result)}"
            ),
            device_id=context.device_id,
            user_id=context.user_id,
            conversation_id=context.conversation_id,
            turn_id=context.turn_id,
            allow_web_search=False,
            timeout_override=getattr(self._agent, "synthesis_timeout_seconds", None),
        )
        if isinstance(final_reply, AgentToolCall):
            raise PolicyViolation("agent requested more than one tool call in a turn")
        return final_reply

    async def next_generation_id(self) -> int:
        """Reserve the next downlink generation for an authenticated turn."""
        async with self._generation_lock:
            self._generation_id = (self._generation_id + 1) & 0xFFFFFFFF
            return self._generation_id

    def _record_stage(
        self,
        *,
        context: ConversationContext,
        stage: str,
        status: str,
        elapsed_ms: int | None = None,
        details: dict[str, object] | None = None,
    ) -> None:
        if self._observer is None:
            return
        self._observer.record_stage(
            device_id=context.device_id,
            turn_id=context.turn_id,
            stage=stage,
            status=status,
            elapsed_ms=elapsed_ms,
            details=details,
        )

    def _record_failure(
        self,
        *,
        context: ConversationContext,
        stage: str,
        started_at: float,
        exc: Exception,
    ) -> None:
        self._record_stage(
            context=context,
            stage=stage,
            status="failed",
            elapsed_ms=self._elapsed_ms(started_at),
            details={"error_type": type(exc).__name__},
        )

    @staticmethod
    def _elapsed_ms(started_at: float) -> int:
        return max(0, round((time.perf_counter() - started_at) * 1_000))
