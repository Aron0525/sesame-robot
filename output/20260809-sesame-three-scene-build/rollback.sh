#!/bin/sh
set -eu
artifact_dir='/Users/mac/Documents/sesame robot/output/20260809-sesame-three-scene-build'
repo='/Users/mac/Documents/sesame robot/endpoint-gateway'
cp "$artifact_dir/originals/scenes.py" "$repo/src/sesame_endpoint_gateway/scenes.py"
cp "$artifact_dir/originals/app.py" "$repo/src/sesame_endpoint_gateway/app.py"
cp "$artifact_dir/originals/openclaw_scene_mcp.py" "$repo/src/sesame_endpoint_gateway/openclaw_scene_mcp.py"
cp "$artifact_dir/originals/index.html" "$repo/src/sesame_endpoint_gateway/static/index.html"
cp "$artifact_dir/originals/README.md" "$repo/README.md"
cp "$artifact_dir/originals/test_device_stream.py" "$repo/tests/test_device_stream.py"
cp "$artifact_dir/originals/test_console_ui.cjs" "$repo/tests/test_console_ui.cjs"
rm -f "$repo/tests/test_scene_profiles.py"
cp "$artifact_dir/openclaw-before/openclaw.json" /Users/mac/.openclaw/openclaw.json
cp "$artifact_dir/openclaw-before/normal-SOUL.md" /Users/mac/.openclaw/workspace-sesame/SOUL.md
rm -rf /Users/mac/.openclaw/workspace-sesame-learning /Users/mac/.openclaw/workspace-sesame-children /Users/mac/.openclaw/workspace-sesame-work
rm -rf /Users/mac/.openclaw/agents/sesame-learning /Users/mac/.openclaw/agents/sesame-children /Users/mac/.openclaw/agents/sesame-work
openclaw config validate --json
openclaw gateway restart --wait 10s --json
