from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol


@dataclass(frozen=True, slots=True)
class AudioFormat:
    codec: str = "opus"
    sample_rate: int = 16_000
    channels: int = 1
    frame_duration_ms: int = 20
    sample_width_bytes: int = 2

    @property
    def samples_per_frame(self) -> int:
        return self.sample_rate * self.frame_duration_ms // 1_000

    @property
    def pcm_bytes_per_frame(self) -> int:
        return self.samples_per_frame * self.channels * self.sample_width_bytes


@dataclass(frozen=True, slots=True)
class AsrResult:
    text: str
    confidence: float | None = None


@dataclass(frozen=True, slots=True)
class VoiceSpec:
    voice_id: str = "sesame_default"
    style: str = "neutral"
    speed: float = 1.0


@dataclass(frozen=True, slots=True)
class ExpressionSpec:
    # The safe neutral expression is a real OLED face. `default` is a legacy
    # protocol compatibility value and must not silently make an agent plan
    # look empty in the operator view.
    name: str = "idle"
    ttl_ms: int = 3_000


@dataclass(frozen=True, slots=True)
class ActionSpec:
    name: str
    duration_ms: int


@dataclass(frozen=True, slots=True)
class AgentResult:
    text: str
    voice: VoiceSpec = field(default_factory=VoiceSpec)
    expression: ExpressionSpec = field(default_factory=ExpressionSpec)
    actions: tuple[ActionSpec, ...] = ()


class AsrProvider(Protocol):
    async def transcribe(self, pcm: bytes, audio_format: AudioFormat) -> AsrResult: ...


class AgentProvider(Protocol):
    async def reply(
        self,
        *,
        text: str,
        device_id: str,
        user_id: str,
        conversation_id: str,
        turn_id: str,
    ) -> AgentResult: ...


class TtsProvider(Protocol):
    async def synthesize(self, text: str, voice: VoiceSpec) -> bytes: ...
