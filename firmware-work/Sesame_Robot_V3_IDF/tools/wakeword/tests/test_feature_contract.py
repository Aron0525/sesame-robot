#!/usr/bin/env python3
"""The trainer must emit the same 33x32 feature shape as ESP32 firmware."""
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


WAKEWORD_DIR = Path(__file__).resolve().parents[1]


class FeatureContractTest(unittest.TestCase):
    def test_silence_maps_to_the_firmware_feature_shape(self) -> None:
        module_path = WAKEWORD_DIR / "train_int8.py"
        self.assertTrue(module_path.is_file(), "int8 trainer is missing")
        spec = importlib.util.spec_from_file_location("train_int8", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        features = module.model_input_features(module.np.zeros(16_000, dtype=module.np.float32))
        self.assertEqual(features.shape, (33, 32))


if __name__ == "__main__":
    unittest.main()
