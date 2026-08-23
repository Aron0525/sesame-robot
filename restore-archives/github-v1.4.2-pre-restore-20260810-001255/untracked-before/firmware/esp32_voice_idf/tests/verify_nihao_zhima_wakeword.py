#!/usr/bin/env python3
"""Verify the embedded 你好芝麻 WakeNet asset and deployment contract."""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "components/sesame_voice/models/zhima_wakeword_tts_v2_int8.tflite"
EMBEDDED = ROOT / "components/sesame_voice/zhima_wakeword_model_data.cpp"
CONFIG = ROOT / "components/sesame_voice/include/sesame_voice/zhima_wakeword_config.h"
ENGINE = ROOT / "components/sesame_voice/wake_vad_engine.cpp"
EXPECTED_SHA256 = "79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065"
EXPECTED_CONFIG = (
    "kFeatureMean = -17.3653469f",
    "kFeatureStd = 2.71605949f",
    "kInputScale = 0.0244557578f",
    "kInputZeroPoint = -77",
    "kWakeThreshold = 0.85f",
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
    assert "smoothed_score" not in engine
    print(f"nihao-zhima WakeNet verified: bytes={len(model)} sha256={EXPECTED_SHA256}")


if __name__ == "__main__":
    main()
