# Sesame Voice Gateway

电脑端应用层语音网关，当前代码位于：

```text
apps/voice_gateway/src/sesame_voice_gateway/
```

模块：

```text
app.py                 FastAPI、WebSocket、设备会话
config.py              环境变量配置
discovery.py           mDNS 服务发布
protocol/control.py    JSON 控制事件
protocol/audio.py      32 字节音频包头
audio/opus.py          libopus 编解码
providers/             ASR、Agent、TTS 接口与 DashScope Provider
openclaw/client.py     OpenClaw Gateway v4 Adapter
pipeline.py            Opus → ASR → Agent → TTS → Opus
observability.py       有界内存事件流、回合状态与本机监控数据
dashboard.html         浏览器运行监控台（仅本机访问）
serial_monitor.py      ESP32 USB 串口日志的状态翻译器（可选）
policy.py              动作和表情安全白名单
```

当前运行链路固定为 DashScope ASR/TTS 加 OpenClaw Gateway。

DashScope ASR/TTS Provider 已有实现。配置 API Key 后可使用公共区域端点；配置 Workspace ID 后自动使用 Workspace 专属端点。本机入口支持由私有 CA 签发的局域网 WSS；量产证书签发、首次配对与设备公钥固定仍是后续安全任务。
