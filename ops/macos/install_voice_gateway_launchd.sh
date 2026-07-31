#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
gateway_dir="$project_root/gateway"
label="com.sesame.voice-gateway"
user_id=$(id -u)
launch_agents_dir="$HOME/Library/LaunchAgents"
logs_dir="$HOME/Library/Logs/SesameVoiceGateway"
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
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>Umask</key>
  <integer>63</integer>
  <key>StandardOutPath</key>
  <string>$logs_dir/gateway.out.log</string>
  <key>StandardErrorPath</key>
  <string>$logs_dir/gateway.err.log</string>
</dict>
</plist>
EOF

launchctl bootout "gui/$user_id/$label" 2>/dev/null || true
launchctl bootstrap "gui/$user_id" "$plist"
launchctl kickstart -k "gui/$user_id/$label"
launchctl print "gui/$user_id/$label" >/dev/null

echo "Voice Gateway launchd service is running: $label"
