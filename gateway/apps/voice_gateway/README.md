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
recordings.py          测试期 PCM → WAV 留样（默认关闭，数量受限）
observability.py       有界内存事件流、回合状态与本机监控数据
dashboard.html         浏览器运行监控台（仅本机访问）
console.html           机器人统一控制台（仅本机访问）
serial_monitor.py      ESP32 USB 串口日志的状态翻译器（可选）
policy.py              动作和表情安全白名单
```

当前运行链路固定为 DashScope ASR/TTS 加 OpenClaw Gateway。

DashScope ASR/TTS Provider 已有实现。配置 API Key 后可使用公共区域端点；配置 Workspace ID 后自动使用 Workspace 专属端点。本机入口支持由私有 CA 签发的局域网 WSS；量产证书签发、首次配对与设备公钥固定仍是后续安全任务。

## 保存 10 段测试录音

默认不保存任何用户音频。测试唤醒、首字截断或 ASR 识别时，显式设置：

```bash
export SESAME_SAVE_TEST_RECORDINGS=true
export SESAME_TEST_RECORDING_LIMIT=10
export SESAME_TEST_RECORDING_DIR=./test-recordings
```

网关会在收到每轮 `listen.stop` 后，将解码后的上行音频保存为 16 kHz、单声道、PCM16 WAV；同时追加 `manifest.jsonl`。达到上限后继续进行 ASR/TTS，但不再落盘。录音默认保存目录已被 Git 忽略；测试结束后应手动删除该目录。
