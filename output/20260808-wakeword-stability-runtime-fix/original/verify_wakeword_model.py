#!/usr/bin/env python3
"""Verify that the embedded wake-word asset is byte-for-byte the selected TFLite."""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "components/sesame_voice/models/zhima_wakeword_tts_v2_int8.tflite"
EMBEDDED = ROOT / "components/sesame_voice/zhima_wakeword_model_data.cpp"
CONFIG = ROOT / "components/sesame_voice/include/sesame_voice/zhima_wakeword_config.h"
EXPECTED_SHA256 = "79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065"


def main() -> None:
    model = MODEL.read_bytes()
    assert len(model) == 22_456, f"unexpected model size: {len(model)}"
    assert hashlib.sha256(model).hexdigest() == EXPECTED_SHA256
    assert model[4:8] == b"TFL3", "not a TFLite FlatBuffer"

    text = EMBEDDED.read_text(encoding="utf-8")
    embedded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", text))
    assert embedded == model, "embedded C++ bytes do not match the .tflite asset"

    config = CONFIG.read_text(encoding="utf-8")
    assert "kWakeThreshold = 0.86f" in config
    assert "kInferenceStrideMs = 200" in config
    print(f"wakeword asset verified: bytes={len(model)} sha256={EXPECTED_SHA256}")


if __name__ == "__main__":
    main()
