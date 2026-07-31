# 电脑端网关运行说明

## 前提

- Python 3.12、`uv`、系统 `libopus` 已安装。
- OpenClaw 仅监听 `127.0.0.1:18789`。
- 已为网关准备私有 TLS 证书和根证书；语音固件只接受 TLS 网关。
- `.env` 中的设备 token 与 ESP32 NVS 中的 `device_token` 相同。
- 普通 DHCP 环境保持 `SESAME_ADVERTISED_IPV4` 未设置；Gateway 会自动刷新 mDNS 地址。

## 启动

```bash
cd "/Users/mac/Desktop/1/SesameV3_语音机器人项目/gateway"
cp .env.example .env
# 填入真实配置，不要提交 .env
make sync
make run
```

检查网关是否启动：

```bash
curl http://127.0.0.1:8765/healthz
```

若启用 TLS，健康检查地址按你配置的证书和主机名访问；ESP32 的设备连接地址由 mDNS `_sesame-gw._tcp.local.` 自动发现。

## 地址和自启动

- Gateway 的 LAN IPv4、ESP32 的网页 IPv4 都可能变化；不要把它们写死在固件或浏览器书签中。
- 对 ESP32 → Gateway，固定的是 `gateway_id`、`sesame-gateway.local` 和 TLS 身份，不是 DHCP IP。固件会在断线后重新发现 mDNS。
- `127.0.0.1:18789` 是 OpenClaw 的回环地址，不受 Wi‑Fi/DHCP 影响；但 OpenClaw 进程本身必须由服务管理器保持运行。
- 先执行 `ops/openclaw/install_openclaw.sh` 安装并启动 OpenClaw 的系统服务。Gateway 自身可使用 `ops/macos/install_voice_gateway_launchd.sh` 安装 macOS `launchd` 自启动服务。

无法承诺“永远不断线”：断电、路由器/网卡故障、客户端隔离、证书或 token 变更、OpenClaw/DashScope 故障都可能中断。当前实现保证的是在同一局域网、凭据和证书未变时，对短暂 Wi‑Fi 中断、DHCP 换址和 Gateway 重启进行有上限的自动恢复。

## 真实模式

在 `.env` 设定：

```dotenv
SESAME_ASR_PROVIDER=dashscope
SESAME_TTS_PROVIDER=dashscope
SESAME_PROVIDER_MODE=openclaw
SESAME_TLS_ENABLED=true
```

还必须提供 DashScope API Key、OpenClaw token、会话密钥、证书路径和设备 token。缺少其中任意一项时，不应烧录语音固件进行实体联调。
