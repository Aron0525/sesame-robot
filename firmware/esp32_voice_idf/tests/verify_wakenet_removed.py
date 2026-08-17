#!/usr/bin/env python3
"""Verify the project no longer ships a custom TFLite wake detector."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/sesame_voice"


def main() -> None:
    cmake = (COMPONENT / "CMakeLists.txt").read_text(encoding="utf-8")
    engine = (COMPONENT / "wake_vad_engine.cpp").read_text(encoding="utf-8")
    config = (COMPONENT / "include/sesame_voice/multinet_wakeword_config.h").read_text(
        encoding="utf-8"
    )
    manifest = (ROOT / "main/idf_component.yml").read_text(encoding="utf-8")

    assert '"zhima_wakeword_model_data.cpp"' not in cmake
    assert "espressif__esp-tflite-micro" not in cmake
    assert "espressif/esp-tflite-micro" not in manifest
    assert "tensorflow/lite" not in engine
    assert "zhima_wakeword_model_data" not in engine
    assert "WakeWordRuntime" in engine
    assert 'kWakeWordPinyin[] = "zhi ma a qi"' in config
    assert 'kMultinetModelName[] = "mn7_cn"' in config
    assert "kWakeThreshold = 0.20f" in config
    assert '"esp_mn_models.h"' in engine
    assert "wakenet_init = false" in engine

    print("TFLite wake detector removal verified")


if __name__ == "__main__":
    main()
