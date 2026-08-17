from __future__ import annotations

import unittest

from sesame_voice_gateway.text_validity import TranscriptDisposition, classify_transcript


class TranscriptValidityTest(unittest.TestCase):
    def test_blank_text_is_discarded(self) -> None:
        self.assertEqual(classify_transcript("  ，！？ "), TranscriptDisposition.BLANK)

    def test_repeated_fillers_are_discarded(self) -> None:
        self.assertEqual(
            classify_transcript("嗯嗯，那个就是啊啊"),
            TranscriptDisposition.FILLER_ONLY,
        )

    def test_filler_prefix_does_not_discard_meaningful_text(self) -> None:
        self.assertEqual(
            classify_transcript("嗯，暂停"),
            TranscriptDisposition.VALID,
        )

    def test_short_semantic_command_is_valid(self) -> None:
        self.assertEqual(classify_transcript("再见"), TranscriptDisposition.VALID)


if __name__ == "__main__":
    unittest.main()
