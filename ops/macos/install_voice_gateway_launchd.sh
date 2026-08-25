#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
gateway_dir="$project_root/gateway"
label="com.sesame.streaming-lab-gateway"
user_id=$(id -u)
launch_agents_dir="$HOME/Library/LaunchAgents"
logs_dir="$HOME/Library/Logs/SesameStreamingLabGateway"
plist="$launch_agents_dir/$label.plist"

if ! command -v uv >/dev/null 2>&1; then
  echo "找不到 uv；请先安装 uv。" >&2
  exit 1
fi
if [ ! -f "$gateway_dir/.env" ]; then
  echo "找不到 $gateway_dir/.env；请先完成 Gateway 配置。" >&2
  exit 1
fi

uv_path=$(command -v uv)
if ! command -v openclaw >/dev/null 2>&1; then
  echo "找不到 openclaw；请先运行 ops/openclaw/install_openclaw.sh。" >&2
  exit 1
fi
openclaw_path=$(command -v openclaw)
runtime_path=$(dirname "$openclaw_path"):/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
mkdir -p "$launch_agents_dir" "$logs_dir"

cat >"$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$label</string>
  <key>ProgramArguments</key>
  <array>
    <string>$uv_path</string>
    <string>run</string>
    <string>--frozen</string>
    <string>sesame-voice-gateway</string>
  </array>
  <key>WorkingDirectory</key>
  <string>$gateway_dir</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key>
    <string>$runtime_path</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>ThrottleInterval</key>
  <integer>5</integer>
  <key>ProcessType</key>
  <string>Background</string>
  <key>Umask</key>
  <integer>63</integer>
  <key>StandardOutPath</key>
  <string>$logs_dir/gateway.out.log</string>
  <key>StandardErrorPath</key>
  <string>$logs_dir/gateway.err.log</string>
</dict>
</plist>
EOF

# 旧标签与正式标签不能并存，否则可能争用端口并广播两套协议。
launchctl disable "gui/$user_id/com.sesame.voice-gateway"
launchctl bootout "gui/$user_id/com.sesame.voice-gateway" 2>/dev/null || true
launchctl enable "gui/$user_id/$label"
launchctl bootout "gui/$user_id/$label" 2>/dev/null || true
attempt=1
while ! launchctl bootstrap "gui/$user_id" "$plist" 2>/dev/null; do
  if [ "$attempt" -ge 10 ]; then
    echo "Voice Gateway launchd 服务注册失败：$label" >&2
    exit 1
  fi
  attempt=$((attempt + 1))
  sleep 1
done
launchctl kickstart -k "gui/$user_id/$label"
launchctl print "gui/$user_id/$label" >/dev/null

echo "Voice Gateway launchd service is running: $label"
