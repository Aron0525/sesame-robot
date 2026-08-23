#!/usr/bin/env bash
set -euo pipefail
repo="${REPO:-/Users/mac/Desktop/1_副本/SesameV3_语音机器人项目}"
patch='/Users/mac/Documents/sesame robot/output/servo-angle-pass-through-desktop-1-copy-20260805/servo-angle-pass-through.patch'
git -C "$repo" apply --check --reverse --whitespace=nowarn "$patch"
git -C "$repo" apply --reverse --whitespace=nowarn "$patch"
echo "rollback_complete=$repo"
