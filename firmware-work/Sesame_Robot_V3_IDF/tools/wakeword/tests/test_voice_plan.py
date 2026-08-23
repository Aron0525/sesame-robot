#!/usr/bin/env python3
"""Contract for the five-voice 你好芝麻 training corpus."""
from __future__ import annotations

import runpy
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[2]


class VoicePlanTest(unittest.TestCase):
    def test_uses_all_five_voices_for_training(self) -> None:
        plan_path = TOOLS_DIR / "wakeword" / "voice_plan.py"
        self.assertTrue(plan_path.is_file(), "five-voice training plan is missing")
        plan = runpy.run_path(str(plan_path))
        voices = plan["VOICE_PLAN"]

        self.assertEqual(len(voices), 5)
        self.assertEqual(len({voice["name"] for voice in voices}), 5)
        self.assertEqual(sum(voice["split"] == "train" for voice in voices), 5)
        self.assertEqual({voice["name"] for voice in voices},
                         {"Xiaoxiao", "Xiaoyi", "Yunxi", "Yunyang", "Yunjian"})


if __name__ == "__main__":
    unittest.main()
