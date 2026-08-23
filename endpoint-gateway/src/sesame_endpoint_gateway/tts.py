"""Local TTS and Opus encoding for the development endpoint gateway."""

from __future__ import annotations

import asyncio
import ctypes.util
import importlib
import subprocess
import tempfile
from pathlib import Path
from typing import Any, Protocol


SAMPLE_RATE = 16_000
SAMPLES_PER_FRAME = 320
PCM_BYTES_PER_FRAME = SAMPLES_PER_FRAME * 2
MAX_PCM_BYTES = SAMPLE_RATE * 2 * 30


class TtsError(RuntimeError):
    """The local gateway could not produce valid playable audio."""


class TtsSynthesizer(Protocol):
    async def synthesize(self, text: str, *, speed: float) -> bytes: ...


class OpusEncoder(Protocol):
    def encode_pcm(self, pcm: bytes) -> tuple[bytes, ...]: ...


class MacSayTtsSynthesizer:
    """Use macOS ``say`` then FFmpeg to create 16 kHz mono S16LE PCM."""

    def __init__(self, *, voice_name: str = "Tingting") -> None:
        self._voice_name = voice_name

    async def synthesize(self, text: str, *, speed: float) -> bytes:
        if not text.strip():
            raise TtsError("TTS text is empty")
        rate = max(90, min(360, round(180 * speed)))
        with tempfile.TemporaryDirectory(prefix="sesame-tts-") as directory:
            source = Path(directory) / "speech.aiff"
            pcm_path = Path(directory) / "speech.pcm"
            await self._run("say", "-v", self._voice_name, "-r", str(rate), "-o", str(source), text)
            await self._run(
                "ffmpeg",
                "-nostdin",
                "-v",
                "error",
                "-y",
                "-i",
                str(source),
                "-ac",
                "1",
                "-ar",
                str(SAMPLE_RATE),
                "-f",
                "s16le",
                str(pcm_path),
            )
            pcm = await asyncio.to_thread(pcm_path.read_bytes)
        if not pcm or len(pcm) > MAX_PCM_BYTES or len(pcm) % 2:
            raise TtsError("TTS PCM is empty, malformed, or exceeds 30 seconds")
        padding = (-len(pcm)) % PCM_BYTES_PER_FRAME
        return pcm + (b"\x00" * padding)

    @staticmethod
    async def _run(*command: str) -> None:
        try:
            process = await asyncio.create_subprocess_exec(
                *command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE
            )
        except FileNotFoundError as exc:
            raise TtsError(f"local TTS dependency is unavailable: {command[0]}") from exc
        _, stderr = await process.communicate()
        if process.returncode != 0:
            detail = stderr.decode("utf-8", errors="replace").strip()
            raise TtsError(f"local TTS command failed: {command[0]}: {detail[:200]}")


class OpusPacketEncoder:
    """Encode exact 20 ms PCM frames using the system libopus library."""

    def __init__(self) -> None:
        opuslib = _load_opuslib()
        self._opuslib = opuslib
        self._encoder = opuslib.Encoder(SAMPLE_RATE, 1, opuslib.APPLICATION_VOIP)

    def encode_pcm(self, pcm: bytes) -> tuple[bytes, ...]:
        if not pcm or len(pcm) % PCM_BYTES_PER_FRAME:
            raise TtsError("PCM must contain exact 20 ms S16LE frames")
        packets: list[bytes] = []
        for offset in range(0, len(pcm), PCM_BYTES_PER_FRAME):
            try:
                packet = bytes(self._encoder.encode(pcm[offset : offset + PCM_BYTES_PER_FRAME], SAMPLES_PER_FRAME))
            except self._opuslib.OpusError as exc:
                raise TtsError(f"Opus encoding failed: {exc}") from exc
            if not packet or len(packet) > 1500:
                raise TtsError("Opus packet is empty or exceeds the SSM1 limit")
            packets.append(packet)
        return tuple(packets)


def _load_opuslib() -> Any:
    """Load opuslib with Homebrew's libopus when macOS cannot discover it."""
    original_find_library = ctypes.util.find_library
    candidates = (Path("/opt/homebrew/lib/libopus.dylib"), Path("/usr/local/lib/libopus.dylib"))
    library = next((path for path in candidates if path.exists()), None)

    def find_library(name: str) -> str | None:
        if name == "opus" and library is not None:
            return str(library)
        return original_find_library(name)

    ctypes.util.find_library = find_library
    try:
        return importlib.import_module("opuslib")
    except ModuleNotFoundError as exc:
        raise TtsError("opuslib is not installed; install the endpoint gateway dependencies") from exc
    finally:
        ctypes.util.find_library = original_find_library
