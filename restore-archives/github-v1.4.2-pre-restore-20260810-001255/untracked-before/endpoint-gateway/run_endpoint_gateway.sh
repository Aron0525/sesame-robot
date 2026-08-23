#!/bin/sh
set -eu
ROOT='/Users/mac/Desktop/2/endpoint-gateway'
cd "$ROOT"
set -a
. "$ROOT/.env"
set +a
export PYTHONPATH="$ROOT/src"
exec "$ROOT/.venv/bin/python" -m uvicorn sesame_endpoint_gateway.server:create_runtime_app --factory --host 127.0.0.1 --port 8788
