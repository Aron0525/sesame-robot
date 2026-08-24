from __future__ import annotations

import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
VOICE_CONTROLLER = (
    PROJECT_ROOT / "components" / "sesame_voice" / "voice_controller.cpp"
)


class TurnCompletionContractTest(unittest.TestCase):
    def test_discarded_turn_releases_the_manual_button_for_the_next_turn(self) -> None:
        """An ASR discard must not leave the firmware forever in Thinking."""
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        dispatcher = source[
            source.index("void VoiceController::process_control_json") : source.index(
                "void VoiceController::begin_tts"
            )
        ]
        branch_start = dispatcher.index('std::strcmp(type, "turn.complete")')
        branch_end = dispatcher.index('std::strcmp(type, "tts.start")', branch_start)
        completion = dispatcher[branch_start:branch_end]

        self.assertIn('string_field(root, "turn_id")', completion)
        self.assertIn('string_field(payload, "outcome")', completion)
        self.assertIn('std::strcmp(outcome, "discard") == 0', completion)
        self.assertIn("turn_id_.data()", completion)
        self.assertIn(
            "turn_state_.apply(sesame::protocol::TurnEvent::kInterrupted)",
            completion,
        )
        self.assertIn("turn_detector_.reset()", completion)
        self.assertIn("voice_capture_enabled_ = true", completion)


if __name__ == "__main__":
    unittest.main()
