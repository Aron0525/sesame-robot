from __future__ import annotations

import ctypes
import dataclasses
import importlib.util
import math
import struct
import subprocess
import sys
import unittest
from pathlib import Path


FIRMWARE_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = FIRMWARE_ROOT.parents[1]
GATEWAY_AUDIO_PROTOCOL = (
    REPOSITORY_ROOT
    / "gateway"
    / "apps"
    / "voice_gateway"
    / "src"
    / "sesame_voice_gateway"
    / "protocol"
    / "audio.py"
)
LIBOPUS = Path("/opt/homebrew/lib/libopus.dylib")
PROBE_PATH = Path(sys.argv[1]) if len(sys.argv) > 1 else None


def load_gateway_audio_protocol():
    spec = importlib.util.spec_from_file_location(
        "sesame_gateway_audio_contract", GATEWAY_AUDIO_PROTOCOL
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load Gateway audio protocol")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    # The firmware host runner uses macOS Python 3.9, while the Gateway targets
    # Python 3.11 and asks dataclass() for slots. Slots do not change the wire
    # packing behavior under test, so strip only that unsupported decorator
    # option while executing the real Gateway protocol module.
    original_dataclass = dataclasses.dataclass

    def compatible_dataclass(*args, **kwargs):
        kwargs.pop("slots", None)
        return original_dataclass(*args, **kwargs)

    dataclasses.dataclass = compatible_dataclass
    try:
        spec.loader.exec_module(module)
    finally:
        dataclasses.dataclass = original_dataclass
    return module


class LibOpus:
    APPLICATION_VOIP = 2048
    SAMPLE_RATE = 16_000
    SAMPLES_PER_FRAME = 320
    MAX_PACKET_BYTES = 400

    def __init__(self) -> None:
        if not LIBOPUS.is_file():
            raise unittest.SkipTest(f"libopus is unavailable at {LIBOPUS}")
        self.library = ctypes.CDLL(str(LIBOPUS))
        self.library.opus_encoder_create.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
        ]
        self.library.opus_encoder_create.restype = ctypes.c_void_p
        self.library.opus_encoder_destroy.argtypes = [ctypes.c_void_p]
        self.library.opus_encode.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_int16),
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_int32,
        ]
        self.library.opus_encode.restype = ctypes.c_int32
        self.library.opus_decoder_create.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
        ]
        self.library.opus_decoder_create.restype = ctypes.c_void_p
        self.library.opus_decoder_destroy.argtypes = [ctypes.c_void_p]
        self.library.opus_decode.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_int32,
            ctypes.POINTER(ctypes.c_int16),
            ctypes.c_int,
            ctypes.c_int,
        ]
        self.library.opus_decode.restype = ctypes.c_int

    def encode_decode_tone(self, frame_count: int) -> tuple[list[int], int]:
        error = ctypes.c_int()
        encoder = self.library.opus_encoder_create(
            self.SAMPLE_RATE, 1, self.APPLICATION_VOIP, ctypes.byref(error)
        )
        if not encoder or error.value != 0:
            raise RuntimeError(f"opus encoder creation failed: {error.value}")
        decoder = self.library.opus_decoder_create(
            self.SAMPLE_RATE, 1, ctypes.byref(error)
        )
        if not decoder or error.value != 0:
            self.library.opus_encoder_destroy(encoder)
            raise RuntimeError(f"opus decoder creation failed: {error.value}")

        packet_sizes: list[int] = []
        decoded_peak = 0
        try:
            encoded = (ctypes.c_ubyte * self.MAX_PACKET_BYTES)()
            decoded = (ctypes.c_int16 * self.SAMPLES_PER_FRAME)()
            for frame_index in range(frame_count):
                pcm = (ctypes.c_int16 * self.SAMPLES_PER_FRAME)(
                    *(
                        round(
                            12_000
                            * math.sin(
                                2
                                * math.pi
                                * 440
                                * (frame_index * self.SAMPLES_PER_FRAME + sample)
                                / self.SAMPLE_RATE
                            )
                        )
                        for sample in range(self.SAMPLES_PER_FRAME)
                    )
                )
                packet_size = self.library.opus_encode(
                    encoder,
                    pcm,
                    self.SAMPLES_PER_FRAME,
                    encoded,
                    self.MAX_PACKET_BYTES,
                )
                self.assert_positive(packet_size, "Opus encode")
                packet_sizes.append(packet_size)
                decoded_samples = self.library.opus_decode(
                    decoder,
                    encoded,
                    packet_size,
                    decoded,
                    self.SAMPLES_PER_FRAME,
                    0,
                )
                if decoded_samples != self.SAMPLES_PER_FRAME:
                    raise AssertionError(
                        f"Opus decoded {decoded_samples} samples instead of 320"
                    )
                decoded_peak = max(decoded_peak, *(abs(value) for value in decoded))
        finally:
            self.library.opus_decoder_destroy(decoder)
            self.library.opus_encoder_destroy(encoder)
        return packet_sizes, decoded_peak

    @staticmethod
    def assert_positive(result: int, operation: str) -> None:
        if result <= 0:
            raise AssertionError(f"{operation} failed with libopus code {result}")


class AudioDownlinkContractTest(unittest.TestCase):
    def test_gateway_wire_bytes_equal_firmware_packer(self) -> None:
        if PROBE_PATH is None:
            self.fail("firmware audio-frame probe path is required")
        firmware_wire = bytes.fromhex(
            subprocess.check_output([PROBE_PATH], text=True).strip()
        )
        gateway = load_gateway_audio_protocol()
        gateway_wire = gateway.pack_audio_frame(
            gateway.AudioFrame(
                direction=gateway.AudioDirection.DOWNLINK,
                flags=0,
                stream_id=2,
                generation_id=0x01020304,
                sequence=5,
                timestamp_ms=0x0102030405060708,
                payload=b"\xF8\xFF\xFE\x00",
            )
        )
        self.assertEqual(gateway_wire, firmware_wire)
        self.assertEqual(len(gateway_wire), struct.calcsize("!4sBBHIIIQI") + 4)

    def test_20_ms_voip_opus_frames_are_nonempty_and_fit_firmware(self) -> None:
        packet_sizes, decoded_peak = LibOpus().encode_decode_tone(30)
        self.assertEqual(len(packet_sizes), 30)
        self.assertTrue(all(0 < size <= LibOpus.MAX_PACKET_BYTES for size in packet_sizes))
        self.assertGreater(decoded_peak, 1_000)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
