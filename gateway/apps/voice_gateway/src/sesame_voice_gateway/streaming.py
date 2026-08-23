"""Sentence-first OpenClaw SSE to PCM/Opus orchestration for the lab Gateway."""

from __future__ import annotations

import asyncio
import time
from collections.abc import AsyncIterator, Callable
from dataclasses import dataclass
from typing import Protocol

from sesame_voice_gateway.openclaw.sse import (
    MAX_TURN_TEXT_CHARS,
    ReplyDelta,
    ReplyFinal,
    ReplySentence,
    ReplyStreamOutput,
)
from sesame_voice_gateway.pipeline import (
    MAX_UPLINK_PACKETS,
    ConversationContext,
    SilentDiscard,
    TurnCodec,
    TurnObserver,
)
from sesame_voice_gateway.policy import validate_asr_result
from sesame_voice_gateway.privacy import validate_remote_pcm, validate_remote_tts_text
from sesame_voice_gateway.providers.base import AsrProvider, AsrResult, AudioFormat, VoiceSpec
from sesame_voice_gateway.text_validity import TranscriptDisposition, classify_transcript


class StreamingAgentProvider(Protocol):
    def stream_reply(
        self,
        *,
        text: str,
        device_id: str,
        user_id: str,
        conversation_id: str,
        turn_id: str,
    ) -> AsyncIterator[ReplyStreamOutput]: ...


class StreamingTtsProvider(Protocol):
    def stream_synthesize(self, text: str, voice: VoiceSpec) -> AsyncIterator[bytes]: ...


@dataclass(frozen=True, slots=True)
class StreamStart:
    generation_id: int
    # This comes from the Gateway, never from an OpenClaw stream event.
    expression: str = "neutral"


@dataclass(frozen=True, slots=True)
class StreamAudio:
    sentence: str
    opus_packets: tuple[bytes, ...]


@dataclass(frozen=True, slots=True)
class StreamFinished:
    # Observability-only in the v1 device protocol. The model cannot alter the
    # response plan after audio starts.
    expression: str


StreamOutput = StreamStart | StreamAudio | StreamFinished | SilentDiscard


@dataclass(frozen=True, slots=True)
class _StreamFailure:
    error: Exception


_PENDING_SENTENCE_LIMIT = MAX_TURN_TEXT_CHARS
_PENDING_AUDIO_CHUNK_LIMIT = 8


class _PcmFrameBuffer:
    def __init__(self, *, frame_size: int) -> None:
        self._frame_size = frame_size
        self._buffer = bytearray()

    def push(self, pcm: bytes) -> tuple[bytes, ...]:
        if not isinstance(pcm, bytes) or not pcm:
            raise ValueError("streaming TTS chunk must be non-empty bytes")
        if len(pcm) % 2:
            raise ValueError("streaming TTS chunk must contain complete S16LE samples")
        self._buffer.extend(pcm)
        frames: list[bytes] = []
        while len(self._buffer) >= self._frame_size:
            frames.append(bytes(self._buffer[: self._frame_size]))
            del self._buffer[: self._frame_size]
        return tuple(frames)

    def finish_sentence(self) -> tuple[bytes, ...]:
        if not self._buffer:
            return ()
        padding = self._frame_size - len(self._buffer)
        self._buffer.extend(b"\x00" * padding)
        frame = bytes(self._buffer)
        self._buffer.clear()
        return (frame,)


class StreamingConversationPipeline:
    """Preserve v1 device transport while streaming only text and PCM internally."""

    def __init__(
        self,
        *,
        codec_factory: Callable[[], TurnCodec],
        asr: AsrProvider,
        agent: StreamingAgentProvider,
        tts: StreamingTtsProvider,
        audio_format: AudioFormat | None = None,
        observer: TurnObserver | None = None,
    ) -> None:
        self._codec_factory = codec_factory
        self._asr = asr
        self._agent = agent
        self._tts = tts
        self._audio_format = audio_format or AudioFormat()
        self._observer = observer
        self._generation_id = 0
        self._generation_lock = asyncio.Lock()

    async def stream_turn(
        self,
        *,
        context: ConversationContext,
        opus_packets: list[bytes],
    ) -> AsyncIterator[StreamOutput]:
        if not opus_packets:
            raise ValueError("a turn requires at least one Opus packet")
        if len(opus_packets) > MAX_UPLINK_PACKETS:
            raise ValueError("turn exceeds maximum audio duration")

        codec = self._codec_factory()
        pcm = b"".join(codec.decode_packet(packet) for packet in opus_packets)
        if not pcm or len(pcm) % self._audio_format.sample_width_bytes:
            raise ValueError("decoded PCM must contain complete samples")
        validate_remote_pcm(pcm)
        asr_started_at = time.perf_counter()
        self._record_stage(
            context=context,
            stage="asr",
            status="started",
            details={"pcm_bytes": len(pcm)},
        )
        try:
            transcript = await self._asr.transcribe(pcm, self._audio_format)
        except Exception as exc:
            self._record_failure(context=context, stage="asr", started_at=asr_started_at, exc=exc)
            raise
        disposition = classify_transcript(transcript.text)
        if disposition != TranscriptDisposition.VALID:
            self._record_stage(
                context=context,
                stage="asr",
                status="discarded",
                elapsed_ms=self._elapsed_ms(asr_started_at),
                details={
                    "transcript": transcript.text,
                    "transcript_chars": len(transcript.text),
                    "reason": disposition.value,
                },
            )
            self._record_stage(
                context=context,
                stage="openclaw",
                status="skipped",
                details={"reason": disposition.value},
            )
            yield SilentDiscard(transcript=transcript, reason=disposition.value)
            return
        transcript = validate_asr_result(transcript)
        self._record_stage(
            context=context,
            stage="asr",
            status="completed",
            elapsed_ms=self._elapsed_ms(asr_started_at),
            details={"transcript": transcript.text, "transcript_chars": len(transcript.text)},
        )

        generation_id = await self.next_generation_id()
        agent_started_at = time.perf_counter()
        self._record_stage(context=context, stage="openclaw", status="started")
        sentence_queue: asyncio.Queue[ReplySentence | ReplyFinal | _StreamFailure] = asyncio.Queue(
            maxsize=_PENDING_SENTENCE_LIMIT
        )
        audio_queue: asyncio.Queue[StreamAudio | StreamFinished | _StreamFailure] = asyncio.Queue(
            maxsize=_PENDING_AUDIO_CHUNK_LIMIT
        )
        agent_task = asyncio.create_task(
            self._read_openclaw(
                context=context,
                transcript=transcript,
                started_at=agent_started_at,
                sentence_queue=sentence_queue,
            ),
            name=f"sesame-openclaw-{context.turn_id}",
        )
        tts_task = asyncio.create_task(
            self._synthesize_sentences(
                codec=codec,
                sentence_queue=sentence_queue,
                audio_queue=audio_queue,
            ),
            name=f"sesame-tts-{context.turn_id}",
        )
        try:
            yield StreamStart(generation_id=generation_id)
            while True:
                output = await audio_queue.get()
                if isinstance(output, _StreamFailure):
                    raise output.error
                yield output
                if isinstance(output, StreamFinished):
                    return
        finally:
            for task in (agent_task, tts_task):
                task.cancel()
            await asyncio.gather(agent_task, tts_task, return_exceptions=True)

    async def _read_openclaw(
        self,
        *,
        context: ConversationContext,
        transcript: AsrResult,
        started_at: float,
        sentence_queue: asyncio.Queue[ReplySentence | ReplyFinal | _StreamFailure],
    ) -> None:
        reply_parts: list[str] = []
        received_delta = False
        try:
            async for output in self._agent.stream_reply(
                text=transcript.text,
                device_id=context.device_id,
                user_id=context.user_id,
                conversation_id=context.conversation_id,
                turn_id=context.turn_id,
            ):
                if isinstance(output, ReplyDelta):
                    received_delta = True
                    reply_parts.append(output.text)
                    reply_text = "".join(reply_parts)
                    self._record_stage(
                        context=context,
                        stage="openclaw",
                        status="streaming",
                        elapsed_ms=self._elapsed_ms(started_at),
                        details={
                            "reply_text": reply_text,
                            "reply_chars": len(reply_text),
                            "sequence": output.sequence,
                        },
                    )
                    continue
                if isinstance(output, ReplySentence):
                    if not received_delta:
                        reply_parts.append(output.text)
                        reply_text = "".join(reply_parts)
                        self._record_stage(
                            context=context,
                            stage="openclaw",
                            status="streaming",
                            elapsed_ms=self._elapsed_ms(started_at),
                            details={
                                "reply_text": reply_text,
                                "reply_chars": len(reply_text),
                                "sequence": output.sequence,
                            },
                        )
                    await sentence_queue.put(output)
                    continue
                if isinstance(output, ReplyFinal):
                    reply_text = "".join(reply_parts)
                    self._record_stage(
                        context=context,
                        stage="openclaw",
                        status="completed",
                        elapsed_ms=self._elapsed_ms(started_at),
                        details={
                            "reply_text": reply_text,
                            "reply_chars": len(reply_text),
                            "expression": output.expression,
                            "actions": [],
                        },
                    )
                    await sentence_queue.put(output)
                    return
                raise ValueError("streaming agent emitted an unsupported output")
            raise ValueError("OpenClaw SSE stream ended without reply.final")
        except Exception as exc:
            self._record_failure(
                context=context,
                stage="openclaw",
                started_at=started_at,
                exc=exc,
            )
            await sentence_queue.put(_StreamFailure(error=exc))

    async def _synthesize_sentences(
        self,
        *,
        codec: TurnCodec,
        sentence_queue: asyncio.Queue[ReplySentence | ReplyFinal | _StreamFailure],
        audio_queue: asyncio.Queue[StreamAudio | StreamFinished | _StreamFailure],
    ) -> None:
        try:
            while True:
                output = await sentence_queue.get()
                if isinstance(output, _StreamFailure):
                    await audio_queue.put(output)
                    return
                if isinstance(output, ReplyFinal):
                    await audio_queue.put(StreamFinished(expression=output.expression))
                    return
                async for audio in self._stream_sentence(codec, output.text):
                    await audio_queue.put(audio)
        except Exception as exc:
            await audio_queue.put(_StreamFailure(error=exc))

    async def _stream_sentence(
        self, codec: TurnCodec, sentence: str
    ) -> AsyncIterator[StreamAudio]:
        validate_remote_tts_text(sentence)
        frame_buffer = _PcmFrameBuffer(frame_size=self._audio_format.pcm_bytes_per_frame)
        packet_count = 0
        async for chunk in self._tts.stream_synthesize(sentence, VoiceSpec()):
            frames = frame_buffer.push(chunk)
            if frames:
                packets = tuple(codec.encode_frame(frame) for frame in frames)
                packet_count += len(packets)
                yield StreamAudio(sentence=sentence, opus_packets=packets)
        tail_frames = frame_buffer.finish_sentence()
        if tail_frames:
            packets = tuple(codec.encode_frame(frame) for frame in tail_frames)
            packet_count += len(packets)
            yield StreamAudio(sentence=sentence, opus_packets=packets)
        if packet_count == 0:
            raise ValueError("streaming TTS returned no PCM audio")

    async def next_generation_id(self) -> int:
        """Reserve the next downlink generation for an authenticated turn."""
        async with self._generation_lock:
            self._generation_id += 1
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
        return max(0, int((time.perf_counter() - started_at) * 1_000))
