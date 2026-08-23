from __future__ import annotations

import importlib
import os
from ctypes.util import find_library
from pathlib import Path
from typing import Any

from sesame_voice_gateway.providers.base import AudioFormat


def _add_homebrew_opus_to_loader_path() -> None:
    if find_library("opus") is not None:
        return

    candidates = (Path("/opt/homebrew/lib"), Path("/usr/local/lib"))
    library_directory = next(
        (path for path in candidates if (path / "libopus.dylib").exists()),
        None,
    )
    if library_directory is None:
        return

    existing = os.environ.get("DYLD_LIBRARY_PATH")
    path_parts = [str(library_directory)]
    if existing:
        path_parts.append(existing)
    os.environ["DYLD_LIBRARY_PATH"] = os.pathsep.join(path_parts)


_add_homebrew_opus_to_loader_path()

opuslib: Any = importlib.import_module("opuslib")


class OpusCodecError(ValueError):
    """Raised when PCM or Opus data violates the configured audio format."""


class OpusCodec:
    def __init__(self, audio_format: AudioFormat | None = None) -> None:
        self.audio_format = audio_format or AudioFormat()
        if self.audio_format.sample_rate != 16_000 or self.audio_format.channels != 1:
            raise OpusCodecError("v1 requires 16000 Hz mono audio")
        self._encoder = opuslib.Encoder(
            self.audio_format.sample_rate,
            self.audio_format.channels,
            opuslib.APPLICATION_VOIP,
        )
        self._decoder = opuslib.Decoder(
            self.audio_format.sample_rate,
            self.audio_format.channels,
        )

    def encode_frame(self, pcm: bytes) -> bytes:
        expected_size = self.audio_format.pcm_bytes_per_frame
        if len(pcm) != expected_size:
            raise OpusCodecError(f"PCM frame must be exactly {expected_size} bytes")
        try:
            return bytes(self._encoder.encode(pcm, self.audio_format.samples_per_frame))
        except opuslib.OpusError as exc:
            raise OpusCodecError(f"Opus encode failed: {exc}") from exc

    def decode_packet(self, packet: bytes) -> bytes:
        if not packet:
            raise OpusCodecError("Opus packet must not be empty")
        try:
            decoded = self._decoder.decode(
                packet,
                self.audio_format.samples_per_frame,
                decode_fec=False,
            )
        except opuslib.OpusError as exc:
            raise OpusCodecError(f"Opus decode failed: {exc}") from exc
        return bytes(decoded)
