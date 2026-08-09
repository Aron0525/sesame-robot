# Sesame V3 语音机器人

这是一个单设备、本机优先的语音机器人项目。唯一运行链路是：

```text
ESP32-S3（麦克风、扬声器、舵机）
  → WSS / Opus → 电脑端 Voice Gateway
  → ASR → OpenClaw（LLM）→ TTS
  → WSS / Opus → ESP32-S3（语音播放、表情、动作）
```

## 目录

```text
firmware/
  esp32_voice_idf/         唯一正式固件：I2S、Opus、WSS、网页控制、OLED、舵机与 NVS 配置
gateway/                   电脑端 FastAPI 网关：ASR、OpenClaw、TTS 编排
contracts/                 ESP32、网关与 OpenClaw 共用的 JSON / 音频协议
docs/                      部署、TLS、云语音、发现和安全说明
ops/openclaw/              固定版本的 OpenClaw 安装入口
```

## 使用顺序

1. 编译并烧录 `firmware/esp32_voice_idf`；它同时包含网页控制与语音闭环，需要 INMP441 麦克风和 MAX98357A 功放。
2. 在电脑上配置并启动 `gateway`，使用真实 ASR/TTS 与 OpenClaw。
3. 用每台设备独立的 NVS 配置烧录 Wi-Fi、设备 token 和网关根证书；这些秘密不进入源码。
4. ESP32 接入 Wi-Fi 后，优先通过 `http://<device_id>.local/` 打开原网页控制台；
   DHCP IPv4 可作为不支持 mDNS 的备用入口。电脑和 ESP32 必须位于同一非访客
   Wi-Fi/VLAN，且网络不得拦截客户端之间的 mDNS 和 TCP 连接。

## v1.4.2：唤醒、动作与连续语音

- **本地唤醒词**：ESP32-S3 内置「你好芝麻」TFLite 唤醒模型；唤醒后播放本地「我在」提示音，再开始采集用户语音。
- **连续语音**：首次说话与 TTS 回复后的追问均使用 VAD 回合检测；语音结束自动提交，回复结束后打开 3 秒追问窗口。
- **动作**：语音响应计划和网页控制共用动作目录；动作、表情、时效和设备状态都在本地校验后执行，`stop` 可随时停止。
- **仍需现场配置**：真实硬件的 I2S 接线、TLS 证书、云端 ASR/TTS 凭据与 OpenClaw token 需要按设备在本机完成配置和联调。

硬件模块、GPIO 和供电关系见 [硬件模块清单](docs/hardware_modules.md)。
