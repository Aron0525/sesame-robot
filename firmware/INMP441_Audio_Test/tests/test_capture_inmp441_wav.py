import importlib.util
import struct
import sys
import tempfile
import types
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).parents[1] / "capture_inmp441_wav.py"
FAKE_SERIAL = types.ModuleType("serial")
FAKE_SERIAL.Serial = object
sys.modules.setdefault("serial", FAKE_SERIAL)
SPEC = importlib.util.spec_from_file_location("capture_inmp441_wav", SCRIPT_PATH)
capture = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(capture)


def wav_bytes(pcm: bytes = b"\x00\x00\x01\x00") -> bytes:
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + len(pcm),
        b"WAVE",
        b"fmt ",
        16,
        1,
        1,
        16000,
        32000,
        2,
        16,
        b"data",
        len(pcm),
    ) + pcm


class CaptureWavTests(unittest.TestCase):
    def test_accepts_expected_mono_pcm_wav(self) -> None:
        capture.validate_wav(wav_bytes())

    def test_rejects_truncated_or_length_mismatched_wav(self) -> None:
        with self.assertRaises(ValueError):
            capture.validate_wav(wav_bytes()[:-1])

    def test_writes_only_validated_wav_atomically(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "capture.wav"
            destination.write_bytes(b"old")
            payload = wav_bytes()

            capture.write_wav_atomically(destination, payload)

            self.assertEqual(destination.read_bytes(), payload)
            self.assertEqual(list(Path(directory).glob(".inmp441-*.tmp")), [])

    def test_invalid_wav_does_not_replace_existing_capture(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "capture.wav"
            destination.write_bytes(b"old")

            with self.assertRaises(ValueError):
                capture.write_wav_atomically(destination, wav_bytes()[:-1])

            self.assertEqual(destination.read_bytes(), b"old")
            self.assertEqual(list(Path(directory).glob(".inmp441-*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
