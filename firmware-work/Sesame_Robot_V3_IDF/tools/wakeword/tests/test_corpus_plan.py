#!/usr/bin/env python3
"""Corpus planning must keep the holdout voice out of model training."""
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


WAKEWORD_DIR = Path(__file__).resolve().parents[1]


class CorpusPlanTest(unittest.TestCase):
    def test_builds_labeled_records_for_all_five_voices(self) -> None:
        module_path = WAKEWORD_DIR / "corpus_plan.py"
        self.assertTrue(module_path.is_file(), "corpus planner is missing")
        spec = importlib.util.spec_from_file_location("corpus_plan", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        records = module.build_corpus_records()
        self.assertEqual(len(records), 50)
        self.assertEqual(sum(record["split"] == "train" for record in records), 25)
        self.assertEqual(sum(record["split"] == "holdout" for record in records), 25)
        self.assertEqual(sum(record["label"] == 1 for record in records), 10)
        self.assertTrue(any(record["phrase"] == "你好，小智" and record["label"] == 0
                            for record in records))

    def test_holdout_rate_stays_inside_the_training_speed_augmentation_range(self) -> None:
        module_path = WAKEWORD_DIR / "corpus_plan.py"
        spec = importlib.util.spec_from_file_location("corpus_plan", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        records = module.build_corpus_records()
        for voice in {record["voice"] for record in records}:
            train = next(record for record in records if record["voice"] == voice and record["split"] == "train" and record["label"] == 1)
            holdout = next(record for record in records if record["voice"] == voice and record["split"] == "holdout" and record["label"] == 1)
            self.assertLessEqual(holdout["speech_rate"] / train["speech_rate"], 1.18)


if __name__ == "__main__":
    unittest.main()
