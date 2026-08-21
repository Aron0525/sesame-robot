#!/usr/bin/env python3
"""Generate a private ESP32-S3 fixed-phrase speaker template.

The feature extractor is shared with the earlier enrollment utility so an
existing local WAV set can be migrated without changing its calibrated math.
The generated header and report contain biometric data and must stay local.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from train_owner_voiceprint import best_features, centroid, cosine, read_pcm16


def render_header(template: list[float], threshold: float, sample_count: int) -> str:
    values = ", ".join(f"{value:.9f}f" for value in template)
    return f'''#pragma once

// GENERATED from local enrollment recordings. This file is intentionally
// Git-ignored because it contains biometric data.

#include "sesame_voice/speaker_verification.h"

namespace sesame::voice::speaker_verification_template {{

inline constexpr SpeakerVerificationTemplate kTemplate{{
    .available = true,
    .threshold = {threshold:.3f}f,
    .centroid = {{{values}}},
}};
inline constexpr int kEnrollmentSampleCount = {sample_count};

}}  // namespace sesame::voice::speaker_verification_template
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", type=Path, required=True)
    parser.add_argument("--output-header", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    wav_paths = sorted(args.samples.glob("*.wav"))
    if len(wav_paths) < 10:
        raise SystemExit("need at least 10 enrolled WAV samples")
    rows: list[list[float]] = []
    rejected: list[str] = []
    for path in wav_paths:
        values = best_features(read_pcm16(path))
        if values is None:
            rejected.append(path.name)
        else:
            rows.append(values)
    if len(rows) < 10:
        raise SystemExit("fewer than 10 recordings have sufficient voiced PCM")

    full_centroid = centroid(rows)
    leave_one_out = [
        cosine(row, centroid(rows[:index] + rows[index + 1 :]))
        for index, row in enumerate(rows)
    ]
    threshold = min(0.995, max(0.80, min(leave_one_out) - 0.005))

    args.output_header.parent.mkdir(parents=True, exist_ok=True)
    args.output_header.write_text(
        render_header(full_centroid, threshold, len(rows)), encoding="utf-8"
    )
    report = {
        "kind": "fixed-phrase local speaker verification",
        "security_grade": False,
        "sample_count": len(rows),
        "rejected_samples": rejected,
        "feature_count": len(full_centroid),
        "capture_ms": 1500,
        "leave_one_out_scores": leave_one_out,
        "recommended_threshold": threshold,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {
                "sample_count": len(rows),
                "rejected_count": len(rejected),
                "recommended_threshold": threshold,
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
