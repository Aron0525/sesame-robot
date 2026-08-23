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
EXPECTED_SHA256 = "2247f91d43b4d70fe17c90b1e999130c2b51476e044a86156b4ea250ba6ae305"
EXPECTED_MODEL_NAME = "zhima_wakeword_dashscope10_int8"
EXPECTED_CONFIG = (
    "kFeatureMean = -17.3126373f",
    "kFeatureStd = 2.72947984f",
    "kInputScale = 0.0243355129f",
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
    assert f'kModelName[] = "{EXPECTED_MODEL_NAME}"' in config
    engine = ENGINE.read_text(encoding="utf-8")
    assert "if (score < zhima::kWakeThreshold" in engine
    assert "smoothed_score" not in engine
    print(f"nihao-zhima WakeNet verified: bytes={len(model)} sha256={EXPECTED_SHA256}")


if __name__ == "__main__":
    main()
