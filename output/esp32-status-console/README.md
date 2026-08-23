# ESP32 状态台

直接用浏览器打开 `index.html` 即可查看页面。页面不提供模拟数据：未连接设备或未收到状态帧时，所有指标会显示为未接入。

ESP32 通过 USB 接到电脑时，先在仓库根目录启动本地串口桥：

```bash
python3 tools/esp32_serial_web_bridge.py --port /dev/cu.usbmodem101
```

然后在页面连接默认地址 `ws://127.0.0.1:8765/status`。浏览器不能直接读取 USB 串口；桥接程序读取串口后，以本机 WebSocket 推送真实遥测。当前桥只解析 `INMP441_Audio_Test` 实际输出的启动、I2S、RMS、Peak、dBFS 和 `QUIET/SOUND`；固件未上报的模块保持“未接入”。

连接已部署的网关时，在页面右上输入 WSS 地址，例如 `wss://sesame-gateway.local/status`。生产环境只接受加密的 `wss://`；仅 `localhost`、`127.0.0.1` 和 `[::1]` 可用于明文 `ws://` 本地开发。任意其他明文地址会被页面拒绝。

本仓库当前没有 ESP32 WebSocket 服务端或 Voice Gateway 实现，因此状态台是一个状态帧客户端，不能单独变成可连接的机器人系统。连接成功后，页面会发送：

```json
{"type":"status.subscribe","payload":{"interval_ms":1000}}
```

设备应每秒回传完整或部分 JSON。页面会合并收到的字段，因此不用一次就实现全部遥测；但只要超过 3 个上报周期未收到新帧，所有值都会被冻结并标记为“数据过期”，不会循环、插值或生成替代值。

```json
{
  "status": "IDLE",
  "sessionId": "sesame-v3-001",
  "generation": 4,
  "uptime": 8231,
  "rssi": -54,
  "latency": 26,
  "heap": 286,
  "temperature": 41.8,
  "dbfs": -35.4,
  "peak": 2240,
  "waveform": [-0.08, 0.12, 0.31, -0.20, 0.04],
  "heartbeat_ms": 1000,
  "servos": [90, 89, 91, 90, 88, 92, 90, 89],
  "modules": {
    "wifi": { "state": "在线", "value": "−54 dBm", "health": "ok" },
    "microphone": { "state": "正常", "value": "16 kHz", "health": "ok" },
    "speaker": { "state": "播放中", "value": "34% 输出", "health": "ok" },
    "oled": { "state": "正常", "value": "talk_happy", "health": "ok" },
    "servos": { "state": "执行中", "value": "wave", "health": "ok" },
    "safety": { "state": "告警", "value": "WSS reconnect", "health": "warn" }
  },
  "event": { "text": "动作 wave 已确认。", "level": "" }
}
```

`status` 仅接受 `IDLE`、`WAKE`、`LISTEN`、`THINK`、`SPEAK`。模块 `health` 可为 `ok`、`warn`、`error`。`waveform` 为最近一次实际采样的归一化波形（`-1` 到 `1`）；不回传该字段时，页面不会绘制伪造声波。
