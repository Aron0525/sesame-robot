#!/bin/zsh
set -euo pipefail

# Runtime credentials stay outside the repository in the deployed gateway's
# private .env. The source tree and the old Desktop/2 checkout are not runtime
# dependencies.
runtime_root="${0:A:h}"
runtime_gateway="${runtime_root}/gateway"
if [[ ! -r "${runtime_gateway}/.env" ]]; then
  print -u2 "missing private runtime configuration: ${runtime_gateway}/.env"
  exit 2
fi
cd "${runtime_gateway}"
exec env \
  PYTHONPATH="${runtime_root}/gateway/apps/voice_gateway/src" \
  "${runtime_root}/gateway/.venv/bin/python" \
  -m sesame_voice_gateway.server_cli
