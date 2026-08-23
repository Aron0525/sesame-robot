#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
WS="$HOME/.openclaw/workspace-sesame"
case "${1:---check}" in
  --check)
    shasum -a 256 -c "$ROOT/original-sha256.txt"
    echo 'rollback snapshot: READY'
    ;;
  --apply)
    shasum -a 256 -c "$ROOT/original-sha256.txt"
    tmp="$HOME/.openclaw/.openclaw.json.rollback.$$"
    cp -p "$ROOT/original/openclaw.json" "$tmp"
    mv -f "$tmp" "$HOME/.openclaw/openclaw.json"
    for src in "$ROOT/original/workspace-sesame"/*; do
      tmp="$WS/.${src:t}.rollback.$$"
      cp -p "$src" "$tmp"
      mv -f "$tmp" "$WS/${src:t}"
    done
    openclaw config validate
    echo 'rollback applied; the Gateway config watcher will reload it.'
    ;;
  *) print -u2 'Usage: rollback.sh [--check|--apply]'; exit 2 ;;
esac
