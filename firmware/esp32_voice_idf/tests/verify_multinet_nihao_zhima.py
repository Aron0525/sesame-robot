#!/usr/bin/env python3
"""Verify the XiaoZhi-style MultiNet “你好，芝麻” firmware contract."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = ROOT / "sdkconfig.defaults"
PARTITIONS = ROOT / "partitions.csv"
CONFIG = ROOT / "components/sesame_voice/include/sesame_voice/multinet_wakeword_config.h"
ENGINE = ROOT / "components/sesame_voice/wake_vad_engine.cpp"
CMAKE = ROOT / "components/sesame_voice/CMakeLists.txt"


def main() -> None:
    defaults = DEFAULTS.read_text(encoding="utf-8")
    assert "CONFIG_SR_MN_CN_MULTINET7_QUANT=y" in defaults
    assert "CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y" not in defaults

    partitions = PARTITIONS.read_text(encoding="utf-8")
    assert "model,     data, spiffs,  0x410000, 0x400000," in partitions
    assert "storage,   data, spiffs,  0x810000, 0x7F0000," in partitions

    config = CONFIG.read_text(encoding="utf-8")
    for value in (
        'kMultinetModelName[] = "mn7_cn"',
        'kWakeWordText[] = "你好，芝麻"',
        'kWakeWordPinyin[] = "ni hao zhi ma"',
        "kWakeThreshold = 0.20f",
        "kDetectionDurationMs = 3000",
    ):
        assert value in config, value

    engine = ENGINE.read_text(encoding="utf-8")
    for value in (
        '#include "esp_mn_models.h"',
        '#include "esp_mn_speech_commands.h"',
        'esp_srmodel_init("model")',
        "esp_mn_commands_add(kWakeCommandId, zhima::kWakeWordPinyin)",
        "multinet->set_det_threshold(model_data, zhima::kWakeThreshold)",
        "afe_config_init(\"M\", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF)",
        "afe_config->wakenet_init = false",
    ):
        assert value in engine, value
    assert "tensorflow/" not in engine
    assert "zhima_wakeword_model_data" not in engine

    cmake = CMAKE.read_text(encoding="utf-8")
    assert "zhima_wakeword_model_data.cpp" not in cmake
    assert "espressif__esp-tflite-micro" not in cmake
    print("XiaoZhi-style MultiNet nihao-zhima contract verified")


if __name__ == "__main__":
    main()
