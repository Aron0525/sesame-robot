#!/usr/bin/env python3
"""Firmware asset rendering preserves the model's trained inference contract."""
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


WAKEWORD_DIR = Path(__file__).resolve().parents[1]


class FirmwareAssetTest(unittest.TestCase):
    def test_renders_model_identity_and_quantization_constants(self) -> None:
        module_path = WAKEWORD_DIR / "firmware_assets.py"
        self.assertTrue(module_path.is_file(), "firmware asset renderer is missing")
        spec = importlib.util.spec_from_file_location("firmware_assets", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        header = module.render_config_header({
            "model_name": "zhima_wakeword_five_tts_int8",
            "threshold": 0.86,
            "tflite": {"bytes": 16, "sha256": "a" * 64, "input": {"scale": 0.02, "zero_point": -77}},
            "feature_contract": {"normalization_mean": -17.3, "normalization_std": 2.7},
        })
        self.assertIn('kModelName[] = "zhima_wakeword_five_tts_int8"', header)
        self.assertIn("kWakeThreshold = 0.86f", header)
        self.assertIn("kInputZeroPoint = -77", header)


if __name__ == "__main__":
    unittest.main()
