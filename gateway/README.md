# Sesame Voice Gateway

`gateway` 是电脑端唯一的语音编排服务：它接收 ESP32 的 Opus 音频，完成 ASR → OpenClaw（LLM）→ TTS，再把语音、表情与动作下发给 ESP32。

## 启动

```bash
cd "/Users/mac/Desktop/1/SesameV3_语音机器人项目/gateway"
cp .env.example .env
# 填写设备 token、DashScope API Key、OpenClaw token 和 TLS 文件路径
make sync
make run
```

语音固件只连接 TLS 网关：`SESAME_TLS_ENABLED=true`，并且 mDNS 广播必须声明 TLS。ESP32 的 NVS 中需写入该 TLS 根证书和与 `.env` 相同的设备 token。

## 运行监控台

网关启动后，在**运行网关的这台电脑**浏览器打开：

```text
https://sesame-gateway.local:8765/dashboard
```

监控台通过 Server-Sent Events 实时显示设备 WSS 状态、`turn_id`、上下行 Opus
统计、ASR、OpenClaw、TTS、动作/表情计划和失败阶段。原始 PCM/Opus 不落盘；页面
仅允许回环地址访问。默认隐藏 ASR 与 TTS 文本，如需短时本机调试，可在 `.env` 临时设置：

```text
SESAME_DASHBOARD_DEBUG_CONTENT=true
```

重启网关后生效；测试结束立即恢复为 `false`。浏览器必须信任该网关的私有根证书，
否则会显示 HTTPS 证书警告。

若 ESP32 通过 USB 接在这台电脑，可在 `.env` 启用 `SESAME_SERIAL_MONITOR_ENABLED=true`
并填写 `SESAME_SERIAL_MONITOR_DEVICE_ID`。Gateway 会直接占用串口，翻译固件的 BOOT、
Opus 上行、`response.plan`、TTS 和播放完成日志为监控台事件。此时不要并行运行
`idf.py monitor`、Arduino Serial Monitor 或 `screen`；一个串口同一时刻只能由一个进程读取。

## 机器人统一控制台

在运行网关的这台电脑浏览器打开：

```text
https://localhost:8765/console
```

控制台复用监控台的实时状态流，并可向已连接 ESP32 下发停止、动作、表情、
单舵机角度和运动参数。此页面与对应 API 仅允许本机访问；设备离线时命令不会入队。

## 运行依赖

- Python 3.12、`uv`、系统 `libopus`。
- 同一局域网内的 ESP32-S3。
- DashScope ASR/TTS。
- 本机回环地址上的 OpenClaw Gateway（只接收 ASR 文本）。

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

协议和设备能力的唯一来源在项目根目录的 [`contracts`](../contracts)。部署与安全说明在 [`docs`](../docs)。
