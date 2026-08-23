#!/bin/sh
set -eu

gateway_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$gateway_dir"

# Tests use deterministic credentials only. They override any local runtime
# .env values and never contact DashScope or OpenClaw.
export SESAME_DASHSCOPE_API_KEY=test-key
export SESAME_ALLOW_REMOTE_SPEECH=true
export SESAME_OPENCLAW_TOKEN=test-openclaw-token
export SESAME_OPENCLAW_SESSION_KEY_SECRET=test-session-secret
export SESAME_DEVICE_TOKENS='{"device_001":"test-device-token"}'
export SESAME_DEVICE_USERS='{"device_001":"user_001"}'

PYTHONPATH=apps/voice_gateway/src uv run --frozen python -m unittest discover -s tests -v
