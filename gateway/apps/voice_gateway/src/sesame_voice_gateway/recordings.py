"""Opt-in local WAV retention for bounded voice-turn diagnostics."""

from __future__ import annotations

import json
import re
import threading
import wave
from dataclasses import dataclass
from pathlib import Path

from sesame_voice_gateway.providers.base import AudioFormat


_RECORDING_NAME = re.compile(r"[^A-Za-z0-9_-]+")


@dataclass(frozen=True, slots=True)
class RecordingArtifact:
    index: int
    path: Path
    duration_ms: int


def create_test_recording_store(
    *, enabled: bool, output_directory: Path, limit: int
) -> TestRecordingStore | None:
    if not enabled:
        return None
    return TestRecordingStore(output_directory=output_directory, limit=limit)


class TestRecordingStore:
    """Persist a small, bounded set of test recordings on the gateway computer."""

    def __init__(self, *, output_directory: Path, limit: int) -> None:
        if limit < 1:
            raise ValueError("recording limit must be positive")
        self._output_directory = output_directory
        self._limit = limit
        self._lock = threading.Lock()
        self._saved_count, self._next_index = self._existing_state()

    def save(
        self,
        *,
        device_id: str,
        turn_id: str,
        pcm: bytes,
        audio_format: AudioFormat,
        asr_text: str,
    ) -> RecordingArtifact | None:
        if not pcm:
            raise ValueError("test recording requires PCM audio")
        if not isinstance(asr_text, str):
            raise ValueError("test recording requires ASR text")
        bytes_per_sample = audio_format.channels * audio_format.sample_width_bytes
        if bytes_per_sample <= 0 or len(pcm) % bytes_per_sample:
            raise ValueError("PCM audio must contain complete samples")

        with self._lock:
            if self._saved_count >= self._limit:
                return None

            self._output_directory.mkdir(parents=True, exist_ok=True)
            index = self._next_index
            filename = f"{index:03d}_{self._safe_filename_part(turn_id)}.wav"
            recording_path = self._output_directory / filename
            self._write_wav(recording_path, pcm, audio_format)

            duration_ms = len(pcm) * 1_000 // (
                audio_format.sample_rate * bytes_per_sample
            )
            self._append_manifest(
                {
                    "asr_text": asr_text,
                    "device_id": device_id,
                    "duration_ms": duration_ms,
                    "file": filename,
                    "index": index,
                    "turn_id": turn_id,
                }
            )
            self._saved_count += 1
            self._next_index += 1
            return RecordingArtifact(index=index, path=recording_path, duration_ms=duration_ms)

    def _existing_state(self) -> tuple[int, int]:
        if not self._output_directory.is_dir():
            return 0, 1
        paths = list(self._output_directory.glob("*.wav"))
        highest_index = 0
        for path in paths:
            prefix = path.name.partition("_")[0]
            if prefix.isdigit():
                highest_index = max(highest_index, int(prefix))
        return len(paths), highest_index + 1

    @staticmethod
    def _safe_filename_part(value: str) -> str:
        safe_value = _RECORDING_NAME.sub("_", value).strip("_")
        return safe_value or "turn"

    @staticmethod
    def _write_wav(path: Path, pcm: bytes, audio_format: AudioFormat) -> None:
        with wave.open(str(path), "wb") as output:
            output.setnchannels(audio_format.channels)
            output.setsampwidth(audio_format.sample_width_bytes)
            output.setframerate(audio_format.sample_rate)
            output.writeframes(pcm)

    def _append_manifest(self, entry: dict[str, object]) -> None:
        manifest_path = self._output_directory / "manifest.jsonl"
        with manifest_path.open("a", encoding="utf-8") as manifest:
            manifest.write(json.dumps(entry, ensure_ascii=False, sort_keys=True))
            manifest.write("\n")
