#!/usr/bin/env python3
"""Verify the selected TFLite asset and its training-time inference contract."""
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
EXPECTED_CONTRACT = (
    "kFeatureMean = -17.381609f",
    "kFeatureStd = 2.55025964f",
    "kInputScale = 0.0250807386f",
    "kInputZeroPoint = -76",
    "kWakeThreshold = 0.82f",
    "kWakeScorePreviousWeight = 0.65f",
    "kWakeScoreCurrentWeight = 0.35f",
    "kInferenceStrideMs = 200",
)


def main() -> None:
    model = MODEL.read_bytes()
    assert len(model) == 22_456, f"unexpected model size: {len(model)}"
    assert hashlib.sha256(model).hexdigest() == EXPECTED_SHA256
    assert model[4:8] == b"TFL3", "not a TFLite FlatBuffer"

    text = EMBEDDED.read_text(encoding="utf-8")
    embedded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", text))
    assert embedded == model, "embedded C++ bytes do not match the .tflite asset"

    config = CONFIG.read_text(encoding="utf-8")
    for expected in EXPECTED_CONTRACT:
        assert expected in config, f"missing model contract value: {expected}"

    engine = ENGINE.read_text(encoding="utf-8")
    assert (
        "zhima::kWakeScorePreviousWeight * smoothed_score +"
        "\n        zhima::kWakeScoreCurrentWeight * score" in engine
    ), "wake score smoothing diverged from the trained-model reference"
    assert "if (smoothed_score < zhima::kWakeThreshold" in engine
    print(f"wakeword asset and inference contract verified: bytes={len(model)} sha256={EXPECTED_SHA256}")


if __name__ == "__main__":
    main()
