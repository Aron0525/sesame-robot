#!/usr/bin/env bash
set -euo pipefail
repo="${REPO:-/Users/mac/Desktop/sesame}"
patch='/Users/mac/Documents/sesame robot/output/servo-angle-pass-through-20260805/servo-angle-pass-through.patch'
git -C "$repo" apply --check --reverse --whitespace=nowarn "$patch"
git -C "$repo" apply --reverse --whitespace=nowarn "$patch"
echo "rollback_complete=$repo"
