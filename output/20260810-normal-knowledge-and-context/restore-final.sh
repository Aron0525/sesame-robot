#!/bin/sh
set -eu
artifact_dir='/Users/mac/Documents/sesame robot/output/20260810-normal-knowledge-and-context'
repo='/Users/mac/Documents/sesame robot/endpoint-gateway'
cp "$artifact_dir/final/repo/scenes.py" "$repo/src/sesame_endpoint_gateway/scenes.py"
cp "$artifact_dir/final/repo/README.md" "$repo/README.md"
cp "$artifact_dir/final/repo/test_scene_profiles.py" "$repo/tests/test_scene_profiles.py"
cp "$artifact_dir/final/workspace-sesame/AGENTS.md" /Users/mac/.openclaw/workspace-sesame/AGENTS.md
cp "$artifact_dir/final/workspace-sesame/USER.md" /Users/mac/.openclaw/workspace-sesame/USER.md
