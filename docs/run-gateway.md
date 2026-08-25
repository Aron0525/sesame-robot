# 电脑端网关运行说明

## 当前部署状态

这台电脑当前运行 **0821 streaming-lab 配置**。ESP32 与 Gateway 必须成对使用同一套发现服务和设备入口，不能把 v1.6 与 0821 混用。

| 配置 | mDNS 服务 | 设备入口 | TLS 主机名 | 端口 |
|---|---|---|---|---:|
| 当前部署：0821 | `_sesame-streamgw._tcp.local.` | `/v2/device-stream` | `sesame-stream-gateway.local` | 8766 |
| 仓库 v1.6 默认值 | `_sesame-gw._tcp.local.` | `/v1/device-stream` | `sesame-gateway.local` | 8765 |

当前 ESP32 已刷入 0821 固件，Gateway 由 macOS LaunchAgent `com.sesame.streaming-lab-gateway` 常驻运行。不要同时启动 `com.sesame.voice-gateway` 或另一份占用 8766 的手动 Gateway。

本机控制台入口：

```text
https://sesame-stream-gateway.local:8766/console
```

如果 macOS 刚恢复服务时 `.local` 缓存尚未刷新，可先使用
`https://127.0.0.1:8766/console`，再用 `dscacheutil -q host -a name
sesame-stream-gateway.local` 检查解析；不要因此改动 ESP 的 Gateway 主机名或 TLS 配置。

## 状态检查

先确认常驻服务和端口：

```bash
launchctl print "gui/$(id -u)/com.sesame.streaming-lab-gateway"
lsof -nP -iTCP:8766 -sTCP:LISTEN
```

再检查 Gateway 和 ESP 会话：

```bash
curl -ksS https://127.0.0.1:8766/healthz
curl -ksS https://127.0.0.1:8766/api/observability/snapshot
```

只有快照同时出现 `online: true` 和 `current_stage: ready`，才能判定 ESP 已连接。端口已监听只说明电脑端进程启动，不代表 mDNS、WSS、设备鉴权已经完成。

## 恢复 LaunchAgent

如果 plist 存在但服务没有运行，先检查标签是否被禁用：

```bash
launchctl print-disabled "gui/$(id -u)" | grep com.sesame.streaming-lab-gateway
```

当前部署的恢复命令是：

```bash
launchctl enable "gui/$(id -u)/com.sesame.streaming-lab-gateway"
launchctl bootstrap "gui/$(id -u)" \
  "$HOME/Library/LaunchAgents/com.sesame.streaming-lab-gateway.plist"
launchctl kickstart -k \
  "gui/$(id -u)/com.sesame.streaming-lab-gateway"
```

`bootstrap 5: Input/output error` 并不一定是 plist 语法错误。本机这次故障的直接原因是标签处于 `disabled`；执行 `launchctl enable` 后，正式服务正常启动，ESP 自动重新完成 mDNS、WSS 和 session ready。

## 前提与边界

- `.env` 中的设备 token 必须与 ESP32 NVS 中的 `device_token` 相同。
- 0821 的直接 OpenClaw Provider 在启动时从 `~/.openclaw/openclaw.json` 读取当前
  Gateway auth token，避免 `.env` 中的旧副本在 OpenClaw 更新或重建后产生
  `token_mismatch`。`SESAME_OPENCLAW_SESSION_KEY_SECRET` 仍只从 `.env` 读取。
- TLS 证书的 DNS SAN 必须包含对应配置的 `.local` 主机名。
- 普通 DHCP 环境保持 `SESAME_ADVERTISED_IPV4` 未设置，Gateway 会刷新 mDNS 地址。
- OpenClaw 只监听 `127.0.0.1:18789`。OpenClaw、ASR 和 TTS 故障不会让已鉴权 ESP 变成离线；它们影响的是连接后的对话处理。
- 当前 ESP32 固件不发送 `playback.stats`。Gateway 只等待一次该可选遥测，随后按绝对
  20 ms 节拍下发；不能在每个 Opus 帧前重复等待 150 ms。
- `ssid=none` 可能来自 macOS 对 Wi-Fi 名称的隐私隐藏，不能单独作为断网证据。应结合活动接口、DHCP 地址、ESP 可达性和 Gateway 会话判断。

服务发现、版本配对与重连规则见[服务发现说明](service-discovery.md)。
