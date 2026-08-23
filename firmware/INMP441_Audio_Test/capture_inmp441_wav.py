#!/usr/bin/env python3
"""Download the WAV made by INMP441_Audio_Test.ino over USB serial.

Install once if needed:
    python3 -m pip install pyserial

Examples:
    python3 capture_inmp441_wav.py --port /dev/cu.usbmodem101 --record
    python3 capture_inmp441_wav.py --port COM7 --output inmp441_test.wav
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import tempfile
import time
from pathlib import Path

try:
    import serial
except ImportError as error:
    raise SystemExit("Missing pyserial. Run: python3 -m pip install pyserial") from error


WAV_HEADER_BYTES = 44
MAX_WAV_BYTES = 10 * 1024 * 1024


def wait_for_line(port: serial.Serial, prefix: str, timeout_seconds: float) -> str:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line:
            print(line)
        if line.startswith(prefix):
            return line
    raise TimeoutError(f"Timed out waiting for {prefix!r}")


def read_exact(port: serial.Serial, total_bytes: int) -> bytes:
    if total_bytes < WAV_HEADER_BYTES or total_bytes > MAX_WAV_BYTES:
        raise ValueError(
            f"WAV size {total_bytes} is outside the allowed range "
            f"({WAV_HEADER_BYTES}–{MAX_WAV_BYTES} bytes)"
        )

    data = bytearray()
    deadline = time.monotonic() + 30
    while len(data) < total_bytes:
        if time.monotonic() > deadline:
            raise TimeoutError(f"Only received {len(data)} of {total_bytes} WAV bytes")
        chunk = port.read(total_bytes - len(data))
        if chunk:
            data.extend(chunk)
            deadline = time.monotonic() + 10
    return bytes(data)


def validate_wav(wav: bytes) -> None:
    """Validate the fixed PCM WAV format emitted by the test firmware.

    The firmware writes a canonical 44-byte RIFF header followed by mono,
    16 kHz, 16-bit PCM samples. Rejecting anything else prevents a corrupt or
    desynchronised serial download from replacing a previously good capture.
    """

    if len(wav) < WAV_HEADER_BYTES:
        raise ValueError(f"WAV is shorter than its {WAV_HEADER_BYTES}-byte header")
    if len(wav) > MAX_WAV_BYTES:
        raise ValueError(f"WAV exceeds the {MAX_WAV_BYTES}-byte download limit")
    if wav[0:4] != b"RIFF" or wav[8:12] != b"WAVE":
        raise ValueError("WAV is missing a RIFF/WAVE header")
    if wav[12:16] != b"fmt " or struct.unpack_from("<I", wav, 16)[0] != 16:
        raise ValueError("WAV must contain a canonical PCM fmt chunk")
    if wav[36:40] != b"data":
        raise ValueError("WAV is missing the expected data chunk")

    riff_size = struct.unpack_from("<I", wav, 4)[0]
    (
        audio_format,
        channels,
        sample_rate,
        byte_rate,
        block_align,
        bits_per_sample,
    ) = struct.unpack_from("<HHIIHH", wav, 20)
    data_size = struct.unpack_from("<I", wav, 40)[0]

    if riff_size + 8 != len(wav):
        raise ValueError("WAV RIFF length does not match the downloaded bytes")
    if data_size + WAV_HEADER_BYTES != len(wav):
        raise ValueError("WAV data length does not match the downloaded bytes")
    if (
        audio_format != 1
        or channels != 1
        or sample_rate != 16000
        or byte_rate != 32000
        or block_align != 2
        or bits_per_sample != 16
    ):
        raise ValueError("WAV format is not mono 16 kHz 16-bit PCM")
    if data_size % block_align != 0:
        raise ValueError("WAV data length is not aligned to whole PCM samples")


def write_wav_atomically(output_path: Path, wav: bytes) -> None:
    """Validate then replace the output only after a complete file is durable."""

    validate_wav(wav)
    destination = Path(output_path)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".inmp441-", suffix=".tmp", dir=destination.parent
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output_file:
            output_file.write(wav)
            output_file.flush()
            os.fsync(output_file.fileno())
        os.replace(temporary_path, destination)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32 USB serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", default="inmp441_test.wav")
    parser.add_argument(
        "--record",
        action="store_true",
        help="Make a fresh 5-second recording before downloading it.",
    )
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.5) as port:
        # Opening the port can reset the board. Let its boot messages finish,
        # then discard them so command responses are unambiguous.
        time.sleep(1.5)
        port.reset_input_buffer()

        if args.record:
            port.write(b"r\n")
            port.flush()
            wait_for_line(port, "REC_DONE|", timeout_seconds=12)

        port.write(b"d\n")
        port.flush()
        header = wait_for_line(port, "WAV_BEGIN ", timeout_seconds=5)
        match = re.fullmatch(r"WAV_BEGIN (\d+)", header)
        if match is None:
            raise RuntimeError(f"Unexpected WAV header: {header!r}")

        wav = read_exact(port, int(match.group(1)))
        write_wav_atomically(Path(args.output), wav)
        print(f"Saved {len(wav)} bytes to {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, TimeoutError, RuntimeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
