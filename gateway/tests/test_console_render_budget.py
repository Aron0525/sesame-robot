from __future__ import annotations

import unittest
from pathlib import Path


_CONSOLE_HTML = (
    Path(__file__).resolve().parents[1]
    / "apps/voice_gateway/src/sesame_voice_gateway/console.html"
)


class ConsoleRenderBudgetTest(unittest.TestCase):
    def test_trace_events_are_coalesced_and_timeline_is_bounded(self) -> None:
        html = _CONSOLE_HTML.read_text(encoding="utf-8")
        self.assertIn("const kRenderedTimelineEvents = 80;", html)
        self.assertIn("const renderEvents = events.slice(0, kRenderedTimelineEvents);", html)
        self.assertIn("function scheduleRender()", html)
        self.assertIn("requestAnimationFrame(() => {", html)
        self.assertIn("mergeEvent(JSON.parse(event.data)); scheduleRender();", html)
        self.assertIn('id="voice-text"', html)
        self.assertIn("function latestTraceText(stageKey, detailKey)", html)
        self.assertIn("latestTraceText('asr', 'transcript')", html)
        self.assertIn("latestTraceText('openclaw', 'reply_text')", html)
        self.assertIn("item.status==='streaming'||item.status==='completed'", html)


if __name__ == "__main__":
    unittest.main()
