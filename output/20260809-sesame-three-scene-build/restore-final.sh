#!/bin/sh
set -eu
artifact_dir='/Users/mac/Documents/sesame robot/output/20260809-sesame-three-scene-build'
repo='/Users/mac/Documents/sesame robot/endpoint-gateway'
for name in scenes.py app.py openclaw_scene_mcp.py; do cp "$artifact_dir/final/repo/$name" "$repo/src/sesame_endpoint_gateway/$name"; done
cp "$artifact_dir/final/repo/index.html" "$repo/src/sesame_endpoint_gateway/static/index.html"
cp "$artifact_dir/final/repo/README.md" "$repo/README.md"
cp "$artifact_dir/final/repo/test_device_stream.py" "$repo/tests/test_device_stream.py"
cp "$artifact_dir/final/repo/test_scene_profiles.py" "$repo/tests/test_scene_profiles.py"
cp "$artifact_dir/final/repo/test_console_ui.cjs" "$repo/tests/test_console_ui.cjs"
cp "$artifact_dir/final/openclaw/openclaw.json" /Users/mac/.openclaw/openclaw.json
for workspace in sesame sesame-learning sesame-children sesame-work; do
  rm -rf "/Users/mac/.openclaw/workspace-$workspace"
  cp -R "$artifact_dir/final/workspaces/workspace-$workspace" "/Users/mac/.openclaw/workspace-$workspace"
done
mkdir -p /Users/mac/.openclaw/agents/sesame-learning/agent /Users/mac/.openclaw/agents/sesame-children/agent /Users/mac/.openclaw/agents/sesame-work/agent
openclaw config validate --json
openclaw gateway restart --wait 10s --json
