import unittest
from pathlib import Path


_CONSOLE_HTML = (
    Path(__file__).resolve().parents[1]
    / "apps/voice_gateway/src/sesame_voice_gateway/console.html"
)


class ConsoleRenderBudgetTest(unittest.TestCase):
    def test_trace_events_are_coalesced_and_timeline_is_bounded(self) -> None:
        html = _CONSOLE_HTML.read_text(encoding="utf-8")
        self.assertIn("const kRenderedTimelineEvents = 240;", html)
        self.assertIn("const renderEvents = events.slice(0, kRenderedTimelineEvents);", html)
        self.assertIn("function renderTimeline(events)", html)
        self.assertIn("const previousScrollTop = root.scrollTop;", html)
        self.assertIn("const anchor = previousRows.find", html)
        self.assertIn("data-event-id=", html)
        self.assertIn("root.scrollTop = nextAnchor.offsetTop + anchorOffset;", html)
        self.assertIn("root.scrollTop = Math.max(0, previousScrollTop", html)
        self.assertIn("function scheduleRender()", html)
        self.assertIn("requestAnimationFrame(() => {", html)
        self.assertIn("mergeEvent(JSON.parse(event.data)); scheduleRender();", html)


if __name__ == "__main__":
    unittest.main()
