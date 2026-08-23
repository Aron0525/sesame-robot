# Sesame Streaming Lab Gateway

`gateway` 是 Streaming Lab 的电脑端语音编排服务：它接收 ESP32 的 Opus 音频，在 ASR final 后先下发安全默认计划，再把 OpenClaw SSE 的完整句子逐句送入 TTS，立即编码为 Opus 并下发给 ESP32。

## 启动

```bash
cd "/Users/mac/Documents/sesame robot/gateway"
# 填写 DashScope API Key；确认后才启用远程语音处理。
make sync
make run
```

macOS 后台服务使用独立运行目录 `~/.local/share/sesame-robot-runtime`，避免依赖
Desktop 上的旧项目副本。首次迁移时把私有 `.env` 和 TLS 证书/私钥放到该运行目录；
私钥和 `.env` 权限设为 `600`，之后同步经过测试的源码：

```bash
install -m 600 /path/to/private.env ~/.local/share/sesame-robot-runtime/gateway/.env
mkdir -p ~/.local/share/sesame-robot-runtime/gateway/.tls
install -m 600 /path/to/server-cert.pem /path/to/server-key.pem \
  ~/.local/share/sesame-robot-runtime/gateway/.tls/
cd "/Users/mac/Documents/sesame robot"
tools/deploy_gateway_runtime.sh
```

部署脚本不会复制、覆盖或删除运行目录中的 `.env` 和 `.tls`。

语音固件只连接 TLS 网关：`SESAME_TLS_ENABLED=true`，并且 mDNS 广播必须声明 TLS。ESP32 的 NVS 中需写入该 TLS 根证书和与 `.env` 相同的设备 token。

## 运行控制台

网关启动后，在**运行网关的这台电脑**浏览器打开：

```text
https://sesame-stream-gateway.local:8766/console
```

控制台把原机器人控制页和 Voice Trace 合并在一起：可执行动作、OLED 表情、8 路舵机、
运动参数、手柄与命令行，同时通过 Server-Sent Events 实时显示设备 WSS 状态、`turn_id`、
上下行 Opus 统计、ASR、OpenClaw、TTS、动作/表情计划和失败阶段。`/dashboard` 保留为
仅监控视图。原始 PCM/Opus 不落盘；页面仅允许本机访问。默认隐藏 ASR 与 TTS 文本，
如需短时本机调试，可在 `.env` 临时设置：

```text
SESAME_DASHBOARD_DEBUG_CONTENT=true
```

重启网关后生效；测试结束立即恢复为 `false`。浏览器必须信任该网关的私有根证书，
否则会显示 HTTPS 证书警告。

若 ESP32 通过 USB 接在这台电脑，可在 `.env` 启用 `SESAME_SERIAL_MONITOR_ENABLED=true`
并填写 `SESAME_SERIAL_MONITOR_DEVICE_ID`。Gateway 会直接占用串口，翻译固件的 BOOT、
Opus 上行、`response.plan`、TTS 和播放完成日志为监控台事件。此时不要并行运行
`idf.py monitor`、Arduino Serial Monitor 或 `screen`；一个串口同一时刻只能由一个进程读取。

## 运行依赖

- Python 3.12、`uv`、系统 `libopus`。
- 同一局域网内的 ESP32-S3。
- DashScope ASR/TTS。
- 本机回环地址上的 OpenClaw SSE bridge（只接收已脱敏的文本请求）。

## 联网搜索

联网搜索默认关闭。它复用现有的 DashScope API Key，但只有 Voice Gateway 能调用；
OpenClaw 只能返回受限的 `web_search` 请求，不能直接访问网络、URL 或 API Key。

```text
SESAME_WEB_SEARCH_ENABLED=true
SESAME_WEB_SEARCH_MODEL=qwen-plus
SESAME_WEB_SEARCH_TIMEOUT_SECONDS=15
```

每轮最多执行一次搜索。Gateway 会校验查询长度与可选时效参数，向 OpenClaw 返回有界、
不可信的搜索证据，再要求它生成最终回复。搜索失败时本轮安全失败，不会回退到任意网页抓取。

OpenClaw 事件与安全边界见项目根目录的 [`contracts/protocols/voice-gateway-openclaw.md`](../contracts/protocols/voice-gateway-openclaw.md)。设备协议与部署说明在 [`contracts`](../contracts) 和 [`docs`](../docs)。
