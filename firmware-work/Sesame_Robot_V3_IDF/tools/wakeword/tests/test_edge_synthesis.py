#!/usr/bin/env python3
"""Edge TTS synthesis must preserve the planned voice ID and split path."""
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


WAKEWORD_DIR = Path(__file__).resolve().parents[1]


class EdgeSynthesisTest(unittest.TestCase):
    def test_builds_an_edge_tts_command_for_the_planned_voice(self) -> None:
        module_path = WAKEWORD_DIR / "synthesize_edge.py"
        self.assertTrue(module_path.is_file(), "Edge TTS synthesizer is missing")
        spec = importlib.util.spec_from_file_location("synthesize_edge", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        command = module.edge_tts_command(
            {"voice_id": "zh-CN-XiaoxiaoNeural", "phrase": "你好，芝麻", "speech_rate": 180},
            Path("/tmp/example.mp3"),
        )
        self.assertIn("zh-CN-XiaoxiaoNeural", command)
        self.assertIn("你好，芝麻", command)
        self.assertEqual(command[-1], "/tmp/example.mp3")


if __name__ == "__main__":
    unittest.main()
