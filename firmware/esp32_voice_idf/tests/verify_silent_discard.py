#!/usr/bin/env python3
"""Verify a gateway discard cannot leave ESP32 stuck in thinking."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/sesame_voice/voice_controller.cpp"
SCHEMA = ROOT.parents[1] / "contracts/schemas/control-event.v1.schema.json"


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    schema = SCHEMA.read_text(encoding="utf-8")
    start = source.index('std::strcmp(type, "turn.complete")')
    end = source.index('std::strcmp(type, "response.plan")', start)
    discard = source[start:end]

    assert '"turn.complete"' in schema
    assert '"outcome"' in schema
    assert '"filler_only"' in schema
    assert "matches_active_turn(inbound_turn)" in discard
    assert "TurnState::kThinking" in discard
    assert "TurnEvent::kDiscarded" in discard
    assert "turn_id_.fill('\\0')" in discard
    assert "start_followup_wait" not in discard
    print("Silent-discard control event returns a thinking turn to idle")


if __name__ == "__main__":
    main()
