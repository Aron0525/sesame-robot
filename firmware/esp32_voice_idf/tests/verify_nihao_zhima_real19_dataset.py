#!/usr/bin/env python3
"""Verify the fixed real-voice evaluation set for the MultiNet wake phrase."""
from __future__ import annotations

import hashlib
import json
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DATASET = ROOT / "training/nihao_zhima_real19_20260810"
MANIFEST = DATASET / "saved_samples_manifest.json"
SAMPLES = DATASET / "samples"


def main() -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert manifest["target_wake_word"] == "你好芝麻"
    samples = manifest["samples"]
    assert len(samples) == 19, len(samples)
    capture_indices = [sample["capture_index"] for sample in samples]
    assert capture_indices == [
        3,
        4,
        5,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        15,
        16,
        17,
        18,
        19,
        20,
        21,
        23,
    ], capture_indices
    for sample in samples:
        path = SAMPLES / sample["file"]
        assert path.is_file(), path
        assert hashlib.sha256(path.read_bytes()).hexdigest() == sample["sha256"]
        with wave.open(str(path), "rb") as audio:
            assert audio.getnchannels() == 1, path
            assert audio.getsampwidth() == 2, path
            assert audio.getframerate() == 16_000, path
            assert audio.getnframes() > 0, path
    print(f"real-voice MultiNet evaluation set verified: {len(samples)} samples")


if __name__ == "__main__":
    main()
