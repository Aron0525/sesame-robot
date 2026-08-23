"""Protocol checks shared by the endpoint gateway and ESP32 firmware.

SSM1 is deliberately parsed before any provider can see audio.  A malformed
frame, an unexpected direction, or a frame above the firmware's 1,500-byte
Opus ceiling never becomes ASR input.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct


SSM1_HEADER_SIZE = 32
MAX_OPUS_PACKET_BYTES = 1500
UPLINK = 0
DOWNLINK = 1


class AudioFrameError(ValueError):
    """A binary device frame violates the fixed SSM1 contract."""


@dataclass(frozen=True)
class AudioFrame:
    direction: int
    flags: int
    stream_id: int
    generation_id: int
    sequence: int
    timestamp_ms: int
    payload: bytes


def pack_audio_frame(
    *,
    direction: int,
    flags: int,
    stream_id: int,
    generation_id: int,
    sequence: int,
    timestamp_ms: int,
    payload: bytes,
) -> bytes:
    """Build one validated SSM1 frame for the device WebSocket."""
    if direction not in (UPLINK, DOWNLINK):
        raise AudioFrameError("audio direction is invalid")
    if not payload or len(payload) > MAX_OPUS_PACKET_BYTES:
        raise AudioFrameError("Opus packet size is outside the allowed range")
    if not 0 <= flags <= 0xFFFF:
        raise AudioFrameError("audio flags are outside the allowed range")
    for value in (stream_id, generation_id, sequence):
        if not 0 <= value <= 0xFFFFFFFF:
            raise AudioFrameError("audio identifier is outside the allowed range")
    if not 0 <= timestamp_ms <= 0xFFFFFFFFFFFFFFFF:
        raise AudioFrameError("audio timestamp is outside the allowed range")
    return struct.pack(
        ">4sBBHIIIQI",
        b"SSM1",
        1,
        direction,
        flags,
        stream_id,
        generation_id,
        sequence,
        timestamp_ms,
        len(payload),
    ) + payload


def unpack_audio_frame(message: bytes, *, expected_direction: int) -> AudioFrame:
    if len(message) < SSM1_HEADER_SIZE:
        raise AudioFrameError("audio frame is shorter than the SSM1 header")
    if message[:4] != b"SSM1":
        raise AudioFrameError("audio frame magic is invalid")
    if message[4] != 1:
        raise AudioFrameError("audio protocol version is unsupported")
    direction = message[5]
    if direction not in (UPLINK, DOWNLINK) or direction != expected_direction:
        raise AudioFrameError("audio direction is not valid for this connection")

    flags, stream_id, generation_id, sequence, timestamp_ms, payload_size = struct.unpack(
        ">HIIIQI", message[6:SSM1_HEADER_SIZE]
    )
    if payload_size == 0 or payload_size > MAX_OPUS_PACKET_BYTES:
        raise AudioFrameError("Opus packet size is outside the allowed range")
    if payload_size != len(message) - SSM1_HEADER_SIZE:
        raise AudioFrameError("audio payload length does not match its header")
    return AudioFrame(
        direction=direction,
        flags=flags,
        stream_id=stream_id,
        generation_id=generation_id,
        sequence=sequence,
        timestamp_ms=timestamp_ms,
        payload=message[SSM1_HEADER_SIZE:],
    )
