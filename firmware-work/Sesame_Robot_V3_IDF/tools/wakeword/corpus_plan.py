"""Deterministic labels and splits for the local five-voice corpus."""
from __future__ import annotations

import sys
from pathlib import Path


MODULE_DIR = Path(__file__).resolve().parent
if str(MODULE_DIR) not in sys.path:
    sys.path.insert(0, str(MODULE_DIR))

from voice_plan import (  # noqa: E402
    GENERAL_NEGATIVE_PHRASES,
    HARD_NEGATIVE_PHRASE,
    TARGET_PHRASE,
    VOICE_PLAN,
)


def _record(
    voice: dict[str, str], phrase: str, label: int, category: str, split: str, speech_rate: int
) -> dict[str, str | int]:
    slug = phrase.replace("，", "_").replace(" ", "")
    return {
        "id": f"{voice['name'].lower()}_{split}_{slug}",
        "voice": voice["name"],
        "voice_id": voice["voice_id"],
        "engine": "Azure Edge Neural TTS",
        "split": split,
        "speech_rate": speech_rate,
        "phrase": phrase,
        "label": label,
        "category": category,
    }


def build_corpus_records() -> list[dict[str, str | int]]:
    """Build target, phonetic hard-negative, and unrelated-speech records."""
    records: list[dict[str, str | int]] = []
    for voice in VOICE_PLAN:
        for split, speech_rate in (("train", 180), ("holdout", 200)):
            records.append(_record(voice, TARGET_PHRASE, 1, "target", split, speech_rate))
            records.append(_record(voice, HARD_NEGATIVE_PHRASE, 0, "hard_negative_nihaoxiaozhi", split, speech_rate))
            records.extend(
                _record(voice, phrase, 0, "general_negative", split, speech_rate)
                for phrase in GENERAL_NEGATIVE_PHRASES
            )
    return records
