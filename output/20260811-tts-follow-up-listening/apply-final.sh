#!/bin/sh
set -eu
project="/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF"
base="/Users/mac/Documents/sesame robot/output/20260811-tts-follow-up-listening/final"
cp "$base/components/sesame_voice/include/sesame_voice/voice_turn_detector.h" "$project/components/sesame_voice/include/sesame_voice/voice_turn_detector.h"
cp "$base/components/sesame_voice/voice_turn_detector.cpp" "$project/components/sesame_voice/voice_turn_detector.cpp"
cp "$base/components/sesame_voice/test/test_voice_turn_detector.cpp" "$project/components/sesame_voice/test/test_voice_turn_detector.cpp"
cp "$base/components/sesame_voice/include/sesame_voice/voice_controller.h" "$project/components/sesame_voice/include/sesame_voice/voice_controller.h"
cp "$base/components/sesame_voice/voice_controller.cpp" "$project/components/sesame_voice/voice_controller.cpp"
