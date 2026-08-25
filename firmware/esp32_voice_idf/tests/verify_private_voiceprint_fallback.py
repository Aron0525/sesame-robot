#!/usr/bin/env python3
"""Verify a clean checkout compiles without a private voiceprint template."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components" / "sesame_voice"


def main() -> None:
    cmake = (COMPONENT / "CMakeLists.txt").read_text(encoding="utf-8")
    controller = (COMPONENT / "voice_controller.cpp").read_text(encoding="utf-8")

    assert 'if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/private")' in cmake
    assert '#if __has_include("owner_voiceprint_template.h")' in controller
    assert "constexpr OwnerVoiceprintTemplate kTemplate{};" in controller
    assert "constexpr int kEnrollmentSampleCount = 0;" in controller

    print("Missing private voiceprint template falls back to a disabled gate")


if __name__ == "__main__":
    main()
