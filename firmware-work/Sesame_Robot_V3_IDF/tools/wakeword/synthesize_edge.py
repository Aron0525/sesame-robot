#!/usr/bin/env python3
"""Create validated 16 kHz PCM training audio with Azure Edge Neural TTS."""
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
REFERENCE_SPEECH_RATE = 180


def wav_path(output_root: Path, record: Mapping[str, str | int]) -> Path:
    return output_root / "wav" / str(record["split"]) / f"{record['id']}.wav"


def wav_contract(path: Path) -> dict[str, int]:
    with wave.open(str(path), "rb") as file:
        return {"channels": file.getnchannels(), "sample_width_bytes": file.getsampwidth(), "sample_rate_hz": file.getframerate(), "frames": file.getnframes()}


def has_expected_wav_contract(path: Path) -> bool:
    if not path.is_file():
        return False
    try:
        contract = wav_contract(path)
    except (EOFError, wave.Error):
        return False
    return contract == {"channels": 1, "sample_width_bytes": 2, "sample_rate_hz": SAMPLE_RATE_HZ, "frames": contract["frames"]} and contract["frames"] >= MINIMUM_SPEECH_FRAMES


def edge_tts_command(record: Mapping[str, str | int], destination: Path) -> list[str]:
    rate_percent = round((int(record["speech_rate"]) / REFERENCE_SPEECH_RATE - 1.0) * 100)
    return [sys.executable, "-m", "edge_tts", "--voice", str(record["voice_id"]), "--text", str(record["phrase"]), "--rate", f"{rate_percent:+d}%", "--write-media", str(destination)]


def synthesize_record(output_root: Path, record: Mapping[str, str | int]) -> dict[str, object]:
    destination = wav_path(output_root, record)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not has_expected_wav_contract(destination):
        with tempfile.TemporaryDirectory(prefix="sesame-edge-tts-") as temporary_directory:
            mp3 = Path(temporary_directory) / "voice.mp3"
            subprocess.run(edge_tts_command(record, mp3), check=True)
            subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", str(mp3), "-ar", str(SAMPLE_RATE_HZ), "-ac", "1", "-c:a", "pcm_s16le", str(destination)], check=True)
    if not has_expected_wav_contract(destination):
        raise RuntimeError(f"invalid synthesized audio contract: {destination}")
    return {**record, "path": str(destination.relative_to(output_root)), "format": wav_contract(destination), "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="artifact directory")
    arguments = parser.parse_args()
    output_root = arguments.output.resolve()
    records = [synthesize_record(output_root, record) for record in build_corpus_records()]
    manifest = {"schema_version": 2, "purpose": "five Azure Edge Neural TTS voice corpus for 你好芝麻", "target_wake_word": "你好，芝麻", "records": records}
    manifest_path = output_root / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"manifest": str(manifest_path), "records": len(records)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
