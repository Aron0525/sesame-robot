#!/bin/sh
set -eu
artifact_dir='/Users/mac/Documents/sesame robot/output/20260809-sesame-mode-selection-prompt'
repo='/Users/mac/Documents/sesame robot/endpoint-gateway'
cp "$artifact_dir/originals/app.py" "$repo/src/sesame_endpoint_gateway/app.py"
cp "$artifact_dir/originals/scenes.py" "$repo/src/sesame_endpoint_gateway/scenes.py"
cp "$artifact_dir/originals/README.md" "$repo/README.md"
rm -f "$repo/src/sesame_endpoint_gateway/mode_selection.py" "$repo/tests/test_mode_selection.py"
