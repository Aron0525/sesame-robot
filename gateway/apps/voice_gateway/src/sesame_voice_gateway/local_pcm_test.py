"""Local-only one-shot PCM fixtures for exercising the authenticated downlink."""

from __future__ import annotations

import asyncio

from sesame_voice_gateway.providers.base import AudioFormat


class LocalPcmTestError(ValueError):
    """Raised when a local PCM fixture is not valid for the v1 audio contract."""


class LocalPcmTestQueue:
    """Stores one complete 16 kHz mono S16LE fixture per device.

    The queue deliberately holds no file paths and has no network-facing API.
    The FastAPI route that uses it is separately restricted to this computer.
    """

    _MAX_DURATION_MS = 20_000

    def __init__(self, audio_format: AudioFormat | None = None) -> None:
        self._audio_format = audio_format or AudioFormat()
        self._pending: dict[str, bytes] = {}
        self._lock = asyncio.Lock()

    async def arm(self, device_id: str, pcm: bytes) -> int:
        frame_size = self._audio_format.pcm_bytes_per_frame
        if not pcm or len(pcm) % frame_size:
            raise LocalPcmTestError(
                f"PCM must contain complete {frame_size}-byte audio frames"
            )
        maximum_bytes = (
            self._audio_format.sample_rate
            * self._audio_format.channels
            * self._audio_format.sample_width_bytes
            * self._MAX_DURATION_MS
            // 1_000
        )
        if len(pcm) > maximum_bytes:
            raise LocalPcmTestError("PCM test audio must be at most 20 seconds")
        async with self._lock:
            self._pending[device_id] = pcm
        return len(pcm) // frame_size

    async def consume(self, device_id: str) -> bytes | None:
        async with self._lock:
            return self._pending.pop(device_id, None)
