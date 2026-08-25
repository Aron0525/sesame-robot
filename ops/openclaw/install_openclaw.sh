#!/bin/sh
set -eu

if ! command -v npm >/dev/null 2>&1; then
  echo "找不到 npm。请先安装 Node.js 22 LTS 或 24。" >&2
  exit 1
fi

npm install -g "openclaw@2026.7.1-1"
openclaw --version
openclaw daemon install
openclaw daemon start
openclaw daemon status
