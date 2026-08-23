#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BASE="$ROOT/output/scene-persona-openclaw-20260806/baseline"
copy() { cp "$1" "$2"; }
copy "$BASE/endpoint-gateway/src/sesame_endpoint_gateway/app.py" "$ROOT/endpoint-gateway/src/sesame_endpoint_gateway/app.py"
copy "$BASE/endpoint-gateway/src/sesame_endpoint_gateway/server.py" "$ROOT/endpoint-gateway/src/sesame_endpoint_gateway/server.py"
copy "$BASE/endpoint-gateway/src/sesame_endpoint_gateway/static.index.html" "$ROOT/endpoint-gateway/src/sesame_endpoint_gateway/static/index.html"
copy "$BASE/endpoint-gateway/README.md" "$ROOT/endpoint-gateway/README.md"
copy "$BASE/endpoint-gateway/tests/test_device_stream.py" "$ROOT/endpoint-gateway/tests/test_device_stream.py"
copy "$BASE/endpoint-gateway/tests/test_console_ui.cjs" "$ROOT/endpoint-gateway/tests/test_console_ui.cjs"
copy "$BASE/openclaw/workspace-sesame/AGENTS.md" /Users/mac/.openclaw/workspace-sesame/AGENTS.md
copy "$BASE/openclaw/workspace-sesame/SOUL.md" /Users/mac/.openclaw/workspace-sesame/SOUL.md
copy "$BASE/openclaw/workspace-sesame/TOOLS.md" /Users/mac/.openclaw/workspace-sesame/TOOLS.md
ROOT="$ROOT" python3 - <<'PY2'
from pathlib import Path
root = Path(__import__('os').environ['ROOT'])
for path in [
    root/'endpoint-gateway/src/sesame_endpoint_gateway/scenes.py',
    root/'endpoint-gateway/src/sesame_endpoint_gateway/openclaw_scene_mcp.py',
    root/'endpoint-gateway/.gitignore',
    root/'endpoint-gateway/.env',
]:
    path.unlink(missing_ok=True)
PY2
openclaw mcp unset sesame-scene
openclaw config set agents.list.1.tools.allow '["read"]' --strict-json
openclaw config set agents.list.1.tools.sandbox.tools.allow '["read"]' --strict-json
openclaw config set agents.list.1.tools.deny '["group:runtime","write","edit","apply_patch","sessions_list","sessions_history","sessions_send","sessions_spawn","sessions_yield","subagents","session_status","group:memory","group:web","group:ui","group:automation","group:messaging","group:nodes","group:agents","group:media","group:openclaw","group:plugins"]' --strict-json
openclaw config set agents.list.1.tools.sandbox.tools.deny '["group:runtime","write","edit","apply_patch","sessions_list","sessions_history","sessions_send","sessions_spawn","sessions_yield","subagents","session_status","group:memory","group:web","group:ui","group:automation","group:messaging","group:nodes","group:agents","group:media","group:openclaw","group:plugins"]' --strict-json
openclaw config validate
openclaw mcp reload
