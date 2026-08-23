from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum

AUDIO_MAGIC = b"SSM1"
AUDIO_PROTOCOL_VERSION = 1
MAX_OPUS_PACKET_BYTES = 1_500
_AUDIO_HEADER = struct.Struct("!4sBBHIIIQI")
AUDIO_HEADER_SIZE = _AUDIO_HEADER.size


class AudioDirection(IntEnum):
    UPLINK = 0
    DOWNLINK = 1


class AudioEncoding(IntEnum):
    OPUS = 0
    PCM_S16LE = 1


# Development-only uplink transport for an ESP32 without an Opus encoder.
# Production audio keeps the default Opus encoding (flags == 0).
AUDIO_FLAG_PCM_S16LE = 0x0001


class AudioProtocolError(ValueError):
    """Raised when a binary audio frame violates the wire contract."""


@dataclass(frozen=True, slots=True)
class AudioFrame:
    direction: AudioDirection
    flags: int
    stream_id: int
    generation_id: int
    sequence: int
    timestamp_ms: int
    payload: bytes

    @property
    def encoding(self) -> AudioEncoding:
        return (
            AudioEncoding.PCM_S16LE
            if self.flags & AUDIO_FLAG_PCM_S16LE
            else AudioEncoding.OPUS
        )


def _validate_uint(name: str, value: int, maximum: int) -> None:
    if not 0 <= value <= maximum:
        raise AudioProtocolError(f"{name} must be between 0 and {maximum}")


def _validate_frame(frame: AudioFrame) -> None:
    _validate_uint("flags", frame.flags, 0xFFFF)
    _validate_uint("stream_id", frame.stream_id, 0xFFFFFFFF)
    _validate_uint("generation_id", frame.generation_id, 0xFFFFFFFF)
    _validate_uint("sequence", frame.sequence, 0xFFFFFFFF)
    _validate_uint("timestamp_ms", frame.timestamp_ms, 0xFFFFFFFFFFFFFFFF)
    if not frame.payload:
        raise AudioProtocolError("payload must not be empty")
    if len(frame.payload) > MAX_OPUS_PACKET_BYTES:
        raise AudioProtocolError("payload exceeds maximum Opus packet size")


def pack_audio_frame(frame: AudioFrame) -> bytes:
    _validate_frame(frame)
    header = _AUDIO_HEADER.pack(
        AUDIO_MAGIC,
        AUDIO_PROTOCOL_VERSION,
        int(frame.direction),
        frame.flags,
        frame.stream_id,
        frame.generation_id,
        frame.sequence,
        frame.timestamp_ms,
        len(frame.payload),
    )
    return header + frame.payload


def unpack_audio_frame(message: bytes) -> AudioFrame:
    if len(message) < AUDIO_HEADER_SIZE:
        raise AudioProtocolError("audio frame is shorter than the fixed header")

    (
        magic,
        version,
        direction_value,
        flags,
        stream_id,
        generation_id,
        sequence,
        timestamp_ms,
        payload_length,
    ) = _AUDIO_HEADER.unpack_from(message)

    if magic != AUDIO_MAGIC:
        raise AudioProtocolError("invalid audio frame magic")
    if version != AUDIO_PROTOCOL_VERSION:
        raise AudioProtocolError(f"unsupported audio protocol version: {version}")
    if payload_length != len(message) - AUDIO_HEADER_SIZE:
        raise AudioProtocolError("audio payload length does not match header")
    if not 0 < payload_length <= MAX_OPUS_PACKET_BYTES:
        raise AudioProtocolError("audio payload length is outside allowed range")

    try:
        direction = AudioDirection(direction_value)
    except ValueError as exc:
        raise AudioProtocolError(f"invalid audio direction: {direction_value}") from exc

    return AudioFrame(
        direction=direction,
        flags=flags,
        stream_id=stream_id,
        generation_id=generation_id,
        sequence=sequence,
        timestamp_ms=timestamp_ms,
        payload=message[AUDIO_HEADER_SIZE:],
    )
