#!/usr/bin/env python3
"""Verify the selected TFLite asset and its trained inference contract."""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "components/sesame_voice/models/zhima_wakeword_tts_v2_int8.tflite"
EMBEDDED = ROOT / "components/sesame_voice/zhima_wakeword_model_data.cpp"
CONFIG = ROOT / "components/sesame_voice/include/sesame_voice/zhima_wakeword_config.h"
ENGINE = ROOT / "components/sesame_voice/wake_vad_engine.cpp"
EXPECTED_SHA256 = "9c20563309845fa620296a278e94877d15de0e0c23fee9bcd156388b764f1949"
EXPECTED_MODEL_NAME = "zhima_wakeword_five_tts_int8"
EXPECTED_CONTRACT = (
    "kFeatureMean = -17.6193867f",
    "kFeatureStd = 2.62766223f",
    "kInputScale = 0.0244624838f",
    "kInputZeroPoint = -80",
    "kWakeThreshold = 0.86f",
    "kInferenceStrideMs = 200",
)


def main() -> None:
    model = MODEL.read_bytes()
    assert len(model) == 22_528, f"unexpected model size: {len(model)}"
    assert hashlib.sha256(model).hexdigest() == EXPECTED_SHA256
    assert model[4:8] == b"TFL3", "not a TFLite FlatBuffer"

    text = EMBEDDED.read_text(encoding="utf-8")
    embedded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", text))
    assert embedded == model, "embedded C++ bytes do not match the .tflite asset"

    config = CONFIG.read_text(encoding="utf-8")
    for expected in EXPECTED_CONTRACT:
        assert expected in config, f"missing model contract value: {expected}"
    assert f'kModelName[] = "{EXPECTED_MODEL_NAME}"' in config

    engine = ENGINE.read_text(encoding="utf-8")
    assert "if (score < threshold" in engine
    assert "set_detection_threshold_hundredths" in engine
    assert "smoothed_score" not in engine, "this model uses direct raw-score thresholding"
    print(f"wakeword asset and trained contract verified: bytes={len(model)} sha256={EXPECTED_SHA256}")


if __name__ == "__main__":
    main()
