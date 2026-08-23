#!/bin/zsh
set -euo pipefail
BASE='https://127.0.0.1:8765'
DEVICE='dev_001'
post() {
  curl -ksS --fail-with-body --max-time 8 \
    -H 'Content-Type: application/json' \
    -d "$1" "$BASE/api/local-control/$DEVICE"
  echo
}
cleanup() { post '{"kind":"stop"}' >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM

echo '阶段 1：现在只驱动 R3（舵机 6 / S5），请观察实际是哪一个实体舵机动了。'
post '{"kind":"servo","servo":6,"angle":45}'
read -r '?看清后按 Enter：'
post '{"kind":"stop"}'

echo '阶段 2：现在只驱动 L4（舵机 8 / S7），请观察实际是哪一个实体舵机动了。'
post '{"kind":"servo","servo":8,"angle":135}'
read -r '?看清后按 Enter 停止：'
post '{"kind":"stop"}'
trap - EXIT INT TERM
echo '测试结束：请记录第一段和第二段各自实际动作的舵机。'
