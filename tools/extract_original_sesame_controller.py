#!/usr/bin/env python3
"""Extract the original ESP32 captive-portal UI for local visual preview.

This copies the exact HTML embedded in the upstream firmware. It does not
emulate ESP32 endpoints and therefore cannot control a robot.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "github_refs/sesame-robot/firmware/captive-portal.h"
DESTINATION = ROOT / "output/original-sesame-controller/index.html"
START = 'R"rawliteral(\n'
END = '\n)rawliteral";'


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index(START) + len(START)
    end = source.index(END, start)
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    DESTINATION.write_text(source[start:end], encoding="utf-8")
    print(f"Extracted original controller to {DESTINATION}")


if __name__ == "__main__":
    main()
