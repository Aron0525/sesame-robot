#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
WS="$HOME/.openclaw/workspace-sesame"
cmp "$ROOT/modified/openclaw.json" "$HOME/.openclaw/openclaw.json"
for f in "$ROOT/modified/workspace-sesame"/*; do
  cmp "$f" "$WS/${f:t}"
done
openclaw config validate >/dev/null
python3 - "$HOME/.openclaw/openclaw.json" "$WS" <<'PY'
from pathlib import Path
import json,sys
config,workspace=sys.argv[1:]
a=next(x for x in json.load(open(config))['agents']['list'] if x.get('id')=='sesame')
assert a['thinkingDefault']=='low'
assert a['contextInjection']=='continuation-skip'
assert a['bootstrapMaxChars']==3500 and a['bootstrapTotalMaxChars']==5500
assert a['contextTokens']==32768
assert a['contextLimits']['postCompactionMaxChars']==1200
assert a['memorySearch']['sources']==['memory']
assert a['tools']['allow']==['read'] and 'group:web' in a['tools']['deny']
files=['AGENTS.md','SOUL.md','TOOLS.md','USER.md','MEMORY.md','IDENTITY.md','HEARTBEAT.md','openclaw-workspace-state.json']
sizes={n:(Path(workspace)/n).stat().st_size for n in files}
assert 2048 <= sizes['AGENTS.md'] <= 3584
assert sum(sizes.values()) <= 5500
print(json.dumps({'settings':'ok','sizes':sizes,'bootstrap_total':sum(sizes.values())},ensure_ascii=False))
PY
diff -ruN "$ROOT/original" "$ROOT/modified" > "$ROOT/.patch.regenerated" || test $? -eq 1
cmp "$ROOT/.patch.regenerated" "$ROOT/patch.diff"
rm -f "$ROOT/.patch.regenerated"
echo 'verification: PASS'
