#!/usr/bin/env bash
set -euo pipefail

gateway='/Users/mac/Desktop/2/gateway'
backup='/Users/mac/Documents/sesame robot/output/20260808-console-realtime-render-fix/original/console.html'

if [[ "${1:-}" == '--dry-run' ]]; then
  printf 'Would restore console.html and remove tests/test_console_render_budget.py.\n'
  exit 0
fi

cp -p "$backup" "$gateway/apps/voice_gateway/src/sesame_voice_gateway/console.html"
python3 - <<'PY'
from pathlib import Path
Path('/Users/mac/Desktop/2/gateway/tests/test_console_render_budget.py').unlink(missing_ok=True)
PY
printf 'Restored console source. Reload /console in the browser to load the restored page.\n'
