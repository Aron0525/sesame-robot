#!/usr/bin/env python3
"""Create the reproducible five-voice local TTS corpus."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import wave
from pathlib import Path
from typing import Mapping


MODULE_DIR = Path(__file__).resolve().parent
if str(MODULE_DIR) not in sys.path:
    sys.path.insert(0, str(MODULE_DIR))

from corpus_plan import build_corpus_records  # noqa: E402


SAMPLE_RATE_HZ = 16_000
MINIMUM_SPEECH_FRAMES = 4_000


def wav_path(output_root: Path, record: Mapping[str, str | int]) -> Path:
    return output_root / "wav" / str(record["split"]) / f"{record['id']}.wav"


def wav_contract(path: Path) -> dict[str, int]:
    with wave.open(str(path), "rb") as file:
        return {
            "channels": file.getnchannels(),
            "sample_width_bytes": file.getsampwidth(),
            "sample_rate_hz": file.getframerate(),
            "frames": file.getnframes(),
        }


def has_expected_wav_contract(path: Path) -> bool:
    if not path.is_file():
        return False
    try:
        contract = wav_contract(path)
    except (EOFError, wave.Error):
        return False
    return (
        contract["channels"] == 1
        and contract["sample_width_bytes"] == 2
        and contract["sample_rate_hz"] == SAMPLE_RATE_HZ
        and contract["frames"] >= MINIMUM_SPEECH_FRAMES
    )


def synthesize_record(output_root: Path, record: Mapping[str, str | int]) -> dict[str, object]:
    destination = wav_path(output_root, record)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not has_expected_wav_contract(destination):
        with tempfile.TemporaryDirectory(prefix="sesame-wakeword-") as temporary_directory:
            aiff = Path(temporary_directory) / "voice.aiff"
            subprocess.run(
                ["say", "-v", str(record["voice"]), "-r", str(record["speech_rate"]), "-o", str(aiff), str(record["phrase"])],
                check=True,
            )
            subprocess.run(
                [
                    "ffmpeg", "-y", "-v", "error", "-i", str(aiff), "-ar", str(SAMPLE_RATE_HZ),
                    "-ac", "1", "-c:a", "pcm_s16le", str(destination),
                ],
                check=True,
            )
    contract = wav_contract(destination)
    required_contract = {"channels": 1, "sample_width_bytes": 2, "sample_rate_hz": SAMPLE_RATE_HZ}
    if any(contract[name] != value for name, value in required_contract.items()):
        raise RuntimeError(f"invalid audio contract for {destination}: {contract}")
    return {
        **record,
        "engine": "macOS say",
        "path": str(destination.relative_to(output_root)),
        "format": contract,
        "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="artifact directory")
    arguments = parser.parse_args()
    output_root = arguments.output.resolve()

    records = [synthesize_record(output_root, record) for record in build_corpus_records()]
    manifest = {
        "schema_version": 1,
        "purpose": "five-voice local-TTS 你好芝麻 wake-word corpus",
        "target_wake_word": "你好，芝麻",
        "records": records,
    }
    manifest_path = output_root / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"manifest": str(manifest_path), "records": len(records)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
