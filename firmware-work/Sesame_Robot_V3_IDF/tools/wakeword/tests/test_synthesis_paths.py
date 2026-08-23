#!/usr/bin/env python3
"""Synthetic speech must be stored in separate train and holdout trees."""
from __future__ import annotations

import importlib.util
import tempfile
import unittest
import wave
from pathlib import Path


WAKEWORD_DIR = Path(__file__).resolve().parents[1]


class SynthesisPathsTest(unittest.TestCase):
    def test_places_each_record_under_its_declared_split(self) -> None:
        module_path = WAKEWORD_DIR / "synthesize_macos.py"
        self.assertTrue(module_path.is_file(), "macOS TTS synthesizer is missing")
        spec = importlib.util.spec_from_file_location("synthesize_macos", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            train = module.wav_path(root, {"id": "eddy_target", "split": "train"})
            holdout = module.wav_path(root, {"id": "sinji_target", "split": "holdout"})

        self.assertEqual(train, root / "wav" / "train" / "eddy_target.wav")
        self.assertEqual(holdout, root / "wav" / "holdout" / "sinji_target.wav")

    def test_recognizes_existing_contract_compliant_wav_for_resume(self) -> None:
        module_path = WAKEWORD_DIR / "synthesize_macos.py"
        self.assertTrue(module_path.is_file(), "macOS TTS synthesizer is missing")
        spec = importlib.util.spec_from_file_location("synthesize_macos", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertTrue(hasattr(module, "has_expected_wav_contract"),
                        "synthesizer cannot resume an interrupted corpus build")

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "sample.wav"
            path.write_bytes(b"not a wav")
            self.assertFalse(module.has_expected_wav_contract(path))

    def test_rejects_a_truncated_placeholder_wav(self) -> None:
        module_path = WAKEWORD_DIR / "synthesize_macos.py"
        spec = importlib.util.spec_from_file_location("synthesize_macos", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "placeholder.wav"
            with wave.open(str(path), "wb") as file:
                file.setnchannels(1)
                file.setsampwidth(2)
                file.setframerate(16_000)
                file.writeframes(b"\x00\x00" * 256)
            self.assertFalse(module.has_expected_wav_contract(path))


if __name__ == "__main__":
    unittest.main()
