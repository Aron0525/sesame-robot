#!/usr/bin/env python3
"""Verify the embedded real-19 “你好，芝麻” model contract."""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "components/sesame_voice/models/nihao_zhima_real19_int8.tflite"
EMBEDDED = ROOT / "components/sesame_voice/zhima_wakeword_model_data.cpp"
CONFIG = ROOT / "components/sesame_voice/include/sesame_voice/zhima_wakeword_config.h"
ENGINE = ROOT / "components/sesame_voice/wake_vad_engine.cpp"
EXPECTED_SHA256 = "6771a84c417ace896a3257321b21d9ab1306a7d03e264c2ffb1a06dccac2dd1a"
EXPECTED_CONFIG = (
    'kModelName[] = "nihao_zhima_real19_int8"',
    "kModelBytes = 22456",
    "kFeatureMean = -17.9795761f",
    "kFeatureStd = 2.57871799f",
    "kInputScale = 0.0263375342f",
    "kInputZeroPoint = -88",
    "kWakeThreshold = 0.92f",
    "kInferenceStrideMs = 200",
)


def main() -> None:
    model = MODEL.read_bytes()
    assert len(model) == 22_456, len(model)
    assert hashlib.sha256(model).hexdigest() == EXPECTED_SHA256
    assert model[4:8] == b"TFL3"
    source = EMBEDDED.read_text(encoding="utf-8")
    embedded = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", source))
    assert embedded == model, "embedded C++ data differs from TFLite asset"
    config = CONFIG.read_text(encoding="utf-8")
    for value in EXPECTED_CONFIG:
        assert value in config, value
    assert 'kWakeWordText[] = "你好芝麻"' in config
    engine = ENGINE.read_text(encoding="utf-8")
    assert "if (score < zhima::kWakeThreshold" in engine
    assert "smoothed_score = 0.65f * smoothed_score + 0.35f * raw_score;" in engine
    print(f"nihao-zhima real-19 model verified: bytes={len(model)} sha256={EXPECTED_SHA256}")


if __name__ == "__main__":
    main()
