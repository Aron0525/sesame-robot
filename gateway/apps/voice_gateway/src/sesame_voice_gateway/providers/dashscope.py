from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import Any, Protocol

import dashscope  # type: ignore[import-untyped]
from dashscope.audio.asr import (  # type: ignore[import-untyped]
    Recognition,
    RecognitionCallback,
    RecognitionResult,
)
from dashscope.audio.http_tts.http_speech_synthesizer import (  # type: ignore[import-untyped]
    HttpSpeechSynthesizer,
)

from sesame_voice_gateway.providers.base import AsrResult, AudioFormat, VoiceSpec
from sesame_voice_gateway.privacy import (
    require_secure_provider_url,
    validate_remote_pcm,
    validate_remote_tts_text,
)

ASR_CHUNK_BYTES = 3_200

_STYLE_INSTRUCTIONS = {
    "happy": "请用开心、自然的语气表达。",
    "sad": "请用低落、克制的语气表达。",
    "angry": "请用严肃、有力度但不过度喊叫的语气表达。",
    "surprised": "请用惊讶、活泼的语气表达。",
    "thinking": "请用思考、稍慢的语气表达。",
}


class DashScopeAudioClientProtocol(Protocol):
    def transcribe(
        self,
        *,
        pcm: bytes,
        model: str,
        sample_rate: int,
        language: str,
    ) -> str: ...

    def synthesize(
        self,
        *,
        text: str,
        model: str,
        voice_id: str,
        sample_rate: int,
        speed: float,
        instruction: str | None,
    ) -> bytes: ...


class _RecognitionCollector(RecognitionCallback):  # type: ignore[misc]
    def __init__(self) -> None:
        self._final_segments: list[str] = []
        self._latest_text = ""
        self.error: RuntimeError | None = None

    @property
    def text(self) -> str:
        if self._final_segments:
            return "".join(self._final_segments).strip()
        return self._latest_text.strip()

    def on_complete(self) -> None:
        return None

    def on_error(self, result: Any) -> None:
        del result
        self.error = RuntimeError("DashScope ASR request failed")

    def on_event(self, result: RecognitionResult) -> None:
        sentence = result.get_sentence()
        text = sentence.get("text")
        if not isinstance(text, str) or not text.strip():
            return
        self._latest_text = text
        if bool(sentence.get("sentence_end")):
            self._final_segments.append(text)


class DashScopeAudioClient:
    def __init__(
        self,
        *,
        api_key: str,
        http_base_url: str,
        websocket_base_url: str,
    ) -> None:
        if not api_key:
            raise ValueError("DashScope API key must not be empty")
        self._api_key = api_key
        self._http_base_url = http_base_url
        self._websocket_base_url = websocket_base_url
        require_secure_provider_url(http_base_url, scheme="https")
        require_secure_provider_url(websocket_base_url, scheme="wss")

    def transcribe(
        self,
        *,
        pcm: bytes,
        model: str,
        sample_rate: int,
        language: str,
    ) -> str:
        validate_remote_pcm(pcm)
        self._configure_sdk()
        callback = _RecognitionCollector()
        recognition = Recognition(
            model=model,
            format="pcm",
            sample_rate=sample_rate,
            language_hints=[language],
            semantic_punctuation_enabled=True,
            heartbeat=True,
            api_key=self._api_key,
            callback=callback,
        )
        recognition.start()
        try:
            for offset in range(0, len(pcm), ASR_CHUNK_BYTES):
                recognition.send_audio_frame(pcm[offset : offset + ASR_CHUNK_BYTES])
        finally:
            recognition.stop()

        if callback.error is not None:
            raise callback.error
        return callback.text

    def synthesize(
        self,
        *,
        text: str,
        model: str,
        voice_id: str,
        sample_rate: int,
        speed: float,
        instruction: str | None,
    ) -> bytes:
        validate_remote_tts_text(text)
        self._configure_sdk()
        stream_result = HttpSpeechSynthesizer.call(
            model=model,
            text=text,
            voice=voice_id,
            audio_format="pcm",
            sample_rate=sample_rate,
            rate=speed,
            instruction=instruction,
            stream=True,
            api_key=self._api_key,
        )
        chunks: list[bytes] = []
        emitted_audio = bytearray()
        for chunk in stream_result:
            if not chunk.audio_url and chunk.audio_data:
                # dashscope's terminal SSE event repeats the full audio that
                # its sentence events already yielded. The SDK exposes no
                # terminal flag, but its cumulative payload is byte-for-byte
                # identical to what has already been emitted.
                if emitted_audio and chunk.audio_data == bytes(emitted_audio):
                    continue
                chunks.append(chunk.audio_data)
                emitted_audio.extend(chunk.audio_data)
        return b"".join(chunks)

    def _configure_sdk(self) -> None:
        dashscope.base_http_api_url = self._http_base_url
        dashscope.base_websocket_api_url = self._websocket_base_url


@dataclass(slots=True)
class DashScopeAsrProvider:
    client: DashScopeAudioClientProtocol
    model: str = "fun-asr-realtime"
    language: str = "zh"
    timeout_seconds: float = 30.0

    async def transcribe(self, pcm: bytes, audio_format: AudioFormat) -> AsrResult:
        if not pcm:
            raise ValueError("ASR requires at least one PCM frame")
        if (
            audio_format.sample_rate != 16_000
            or audio_format.channels != 1
            or audio_format.sample_width_bytes != 2
        ):
            raise ValueError("DashScope ASR requires 16 kHz mono PCM S16LE")
        if len(pcm) % audio_format.sample_width_bytes:
            raise ValueError("PCM byte length is not sample aligned")
        validate_remote_pcm(pcm)

        transcript = await asyncio.wait_for(
            asyncio.to_thread(
                self.client.transcribe,
                pcm=pcm,
                model=self.model,
                sample_rate=audio_format.sample_rate,
                language=self.language,
            ),
            timeout=self.timeout_seconds,
        )
        if not transcript.strip():
            raise RuntimeError("DashScope ASR returned an empty transcript")
        return AsrResult(text=transcript.strip(), confidence=None)


@dataclass(slots=True)
class DashScopeTtsProvider:
    client: DashScopeAudioClientProtocol
    default_voice_id: str = "longanhuan_v3.6"
    model: str = "qwen-audio-3.0-tts-flash"
    timeout_seconds: float = 30.0
    audio_format: AudioFormat = AudioFormat()

    async def synthesize(self, text: str, voice: VoiceSpec) -> bytes:
        if not text.strip():
            raise ValueError("TTS input text must not be empty")
        validate_remote_tts_text(text)
        voice_id = (
            self.default_voice_id
            if voice.voice_id in {"", "sesame_default"}
            else voice.voice_id
        )
        raw_pcm = await asyncio.wait_for(
            asyncio.to_thread(
                self.client.synthesize,
                text=text,
                model=self.model,
                voice_id=voice_id,
                sample_rate=self.audio_format.sample_rate,
                speed=voice.speed,
                instruction=_STYLE_INSTRUCTIONS.get(voice.style),
            ),
            timeout=self.timeout_seconds,
        )
        if not raw_pcm:
            raise RuntimeError("DashScope TTS returned empty PCM audio")
        if len(raw_pcm) % self.audio_format.sample_width_bytes:
            raise RuntimeError("DashScope TTS returned sample-misaligned PCM audio")
        return _pad_to_complete_frames(raw_pcm, self.audio_format)


def _pad_to_complete_frames(pcm: bytes, audio_format: AudioFormat) -> bytes:
    frame_size = audio_format.pcm_bytes_per_frame
    remainder = len(pcm) % frame_size
    if remainder == 0:
        return pcm
    return pcm + b"\x00" * (frame_size - remainder)
