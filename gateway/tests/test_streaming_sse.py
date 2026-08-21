from __future__ import annotations

import unittest

from sesame_voice_gateway.openclaw.sse import ReplyFinal, ReplySentence, SseReplyParser


class SseReplyParserTest(unittest.TestCase):
    def test_delta_and_sentence_are_joined_once_at_a_sentence_boundary(self) -> None:
        parser = SseReplyParser(expected_turn_id="turn_01")

        delta_events = parser.feed(
            event="reply.delta", data='{"turn_id":"turn_01","seq":1,"text":"今天"}'
        )
        self.assertEqual(tuple(event.text for event in delta_events), ("今天",))
        events = parser.feed(
            event="reply.sentence",
            data='{"turn_id":"turn_01","seq":2,"text":"天气不错。"}',
        )

        self.assertEqual(
            tuple(event.text for event in events),
            ("天气不错。", "今天天气不错。"),
        )

    def test_final_rejects_actions_and_finishes_unpunctuated_tail(self) -> None:
        parser = SseReplyParser(expected_turn_id="turn_01")
        parser.feed(event="reply.delta", data='{"turn_id":"turn_01","seq":1,"text":"明天见"}')

        events = parser.feed(
            event="reply.final",
            data='{"turn_id":"turn_01","seq":2,"expression":"happy","actions":[]}',
        )

        self.assertEqual(
            events,
            (
                ReplySentence(text="明天见", sequence=2),
                ReplyFinal(expression="happy", sequence=2),
            ),
        )

        with self.assertRaisesRegex(ValueError, "actions"):
            SseReplyParser(expected_turn_id="turn_01").feed(
                event="reply.final",
                data='{"turn_id":"turn_01","seq":1,"expression":"happy","actions":["wave"]}',
            )

    def test_rejects_out_of_order_or_foreign_turn_events(self) -> None:
        parser = SseReplyParser(expected_turn_id="turn_01")
        parser.feed(event="reply.delta", data='{"turn_id":"turn_01","seq":2,"text":"你好"}')

        with self.assertRaisesRegex(ValueError, "sequence"):
            parser.feed(event="reply.delta", data='{"turn_id":"turn_01","seq":2,"text":"。"}')
        with self.assertRaisesRegex(ValueError, "turn_id"):
            parser.feed(event="reply.delta", data='{"turn_id":"turn_02","seq":3,"text":"。"}')


if __name__ == "__main__":
    unittest.main()
