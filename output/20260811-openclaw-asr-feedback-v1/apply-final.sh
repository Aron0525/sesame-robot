#!/bin/sh
set -eu
base="/Users/mac/Documents/sesame robot/output/20260811-openclaw-asr-feedback-v1/final"
cp "$base/sesame_endpoint_gateway-app.py" "/Users/mac/Documents/sesame robot/endpoint-gateway/src/sesame_endpoint_gateway/app.py"
cp "$base/tests-test_device_stream.py" "/Users/mac/Documents/sesame robot/endpoint-gateway/tests/test_device_stream.py"
cp "$base/endpoint-gateway-README.md" "/Users/mac/Documents/sesame robot/endpoint-gateway/README.md"
cp "$base/workspace-sesame-AGENTS.md" "/Users/mac/.openclaw/workspace-sesame/AGENTS.md"
cp "$base/workspace-sesame-learning-AGENTS.md" "/Users/mac/.openclaw/workspace-sesame-learning/AGENTS.md"
cp "$base/workspace-sesame-children-AGENTS.md" "/Users/mac/.openclaw/workspace-sesame-children/AGENTS.md"
cp "$base/workspace-sesame-work-AGENTS.md" "/Users/mac/.openclaw/workspace-sesame-work/AGENTS.md"
