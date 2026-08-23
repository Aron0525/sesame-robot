#!/bin/zsh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
while IFS=$'\t' read -r target storage; do
  mkdir -p "$(dirname "$target")"
  cp "$SCRIPT_DIR/finals/$storage" "$target"
  shasum -a 256 "$target"
done < "$SCRIPT_DIR/changed-files.tsv"
