#!/bin/zsh
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
runtime_root="${SESAME_GATEWAY_RUNTIME_ROOT:-${HOME}/.local/share/sesame-robot-runtime}"

if [[ -z "${runtime_root}" || "${runtime_root}" == "/" || "${runtime_root}" == "${HOME}" ]]; then
  print -u2 "refusing unsafe runtime target: ${runtime_root}"
  exit 2
fi

mkdir -p "${runtime_root}/gateway" "${runtime_root}/contracts"
rsync -a --delete-after \
  --exclude '.venv/' \
  --exclude '.pytest_cache/' \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '.env' \
  --exclude '.tls/' \
  --exclude 'recordings/' \
  "${project_root}/gateway/" "${runtime_root}/gateway/"
rsync -a --delete-after \
  "${project_root}/contracts/" "${runtime_root}/contracts/"
rsync -a "${project_root}/run-gateway-launchagent.sh" \
  "${runtime_root}/run-gateway-launchagent.sh"
chmod 755 "${runtime_root}/run-gateway-launchagent.sh"

cd "${runtime_root}/gateway"
/opt/homebrew/bin/uv sync --frozen
