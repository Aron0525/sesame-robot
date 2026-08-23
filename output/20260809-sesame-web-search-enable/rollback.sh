#!/bin/sh
set -eu
cp "/Users/mac/Documents/sesame robot/output/20260809-sesame-web-search-enable/openclaw.json.before" "/Users/mac/.openclaw/openclaw.json"
cp "/Users/mac/Documents/sesame robot/output/20260809-sesame-web-search-enable/AGENTS.md.before" "/Users/mac/.openclaw/workspace-sesame/AGENTS.md"
cp "/Users/mac/Documents/sesame robot/output/20260809-sesame-web-search-enable/TOOLS.md.before" "/Users/mac/.openclaw/workspace-sesame/TOOLS.md"
openclaw config validate --json
openclaw gateway restart --wait 10s --json
