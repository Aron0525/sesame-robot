#!/usr/bin/env python3
"""Install a gated TFLite model and its exact inference contract into firmware."""
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path
from typing import Any


DFT_BINS = (6, 10, 13, 17, 21, 24, 28, 32, 35, 39, 43, 46, 50, 54, 57, 61,
            65, 69, 72, 76, 80, 83, 87, 91, 94, 98, 102, 105, 109, 113, 116, 120)


def render_config_header(metrics: dict[str, Any]) -> str:
    tflite = metrics["tflite"]
    feature = metrics["feature_contract"]
    bins = ", ".join(str(value) for value in DFT_BINS)
    return f'''#pragma once

#include <cstddef>
#include <cstdint>

namespace sesame::voice::zhima {{

inline constexpr char kModelName[] = "{metrics['model_name']}";
inline constexpr char kModelSha256[] = "{tflite['sha256']}";
inline constexpr size_t kModelBytes = {tflite['bytes']};

inline constexpr uint32_t kSampleRateHz = 16000;
inline constexpr size_t kClipSamples = 16000;
inline constexpr size_t kFrameSize = 480;
inline constexpr size_t kFrameCount = 33;
inline constexpr size_t kFeatureBins = 32;
inline constexpr int kDftBins[kFeatureBins] = {{{bins}}};
inline constexpr float kFeatureMean = {float(feature['normalization_mean']):.9g}f;
inline constexpr float kFeatureStd = {float(feature['normalization_std']):.9g}f;
inline constexpr float kInputScale = {float(tflite['input']['scale']):.9g}f;
inline constexpr int kInputZeroPoint = {int(tflite['input']['zero_point'])};

inline constexpr float kWakeThreshold = {float(metrics['threshold']):.2f}f;
inline constexpr uint32_t kInferenceStrideMs = 200;
inline constexpr uint32_t kWakeCooldownMs = 2500;

}}  // namespace sesame::voice::zhima
'''


def render_model_source(model_bytes: bytes) -> str:
    rows = []
    for offset in range(0, len(model_bytes), 12):
        values = ", ".join(f"0x{value:02x}" for value in model_bytes[offset : offset + 12])
        rows.append(f"    {values},")
    return "\n".join((
        '#include "sesame_voice/zhima_wakeword_model_data.h"',
        "",
        "namespace sesame::voice::zhima {",
        "",
        "alignas(16) const unsigned char kModelData[] = {",
        *rows,
        "};",
        "",
        "const size_t kModelDataLen = sizeof(kModelData);",
        "",
        "}  // namespace sesame::voice::zhima",
        "",
    ))


def install_assets(model_path: Path, metrics_path: Path, firmware_root: Path) -> dict[str, str | int]:
    metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
    if not metrics.get("deployment_gate"):
        raise ValueError("refusing to install a model that did not pass the deployment gate")
    model_bytes = model_path.read_bytes()
    if len(model_bytes) != int(metrics["tflite"]["bytes"]):
        raise ValueError("model byte length differs from training metrics")
    voice_root = firmware_root / "components" / "sesame_voice"
    shutil.copyfile(model_path, voice_root / "models" / "zhima_wakeword_tts_v2_int8.tflite")
    (voice_root / "zhima_wakeword_model_data.cpp").write_text(render_model_source(model_bytes), encoding="utf-8")
    (voice_root / "include" / "sesame_voice" / "zhima_wakeword_config.h").write_text(render_config_header(metrics), encoding="utf-8")
    return {"model_name": str(metrics["model_name"]), "bytes": len(model_bytes), "sha256": str(metrics["tflite"]["sha256"])}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--metrics", required=True, type=Path)
    parser.add_argument("--firmware-root", type=Path, default=Path(__file__).resolve().parents[2])
    arguments = parser.parse_args()
    print(json.dumps(install_assets(arguments.model, arguments.metrics, arguments.firmware_root.resolve()), ensure_ascii=False))


if __name__ == "__main__":
    main()
