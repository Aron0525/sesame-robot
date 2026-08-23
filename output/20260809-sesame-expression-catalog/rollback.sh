#!/bin/sh
set -eu
cp "/Users/mac/Documents/sesame robot/output/20260809-sesame-expression-catalog/AGENTS.md.before" "/Users/mac/.openclaw/workspace-sesame/AGENTS.md"
shasum -a 256 "/Users/mac/.openclaw/workspace-sesame/AGENTS.md"
