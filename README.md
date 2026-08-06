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

## v1.4.1 项目状态

完整说明见 [v1.4.1 发布说明](docs/releases/v1.4.1.md)。本版本按当前代码与测试结果记录状态。

### 已实现

- 本地语音唤醒链路已接入：WakeNet/VAD 检测到唤醒后播放本地提示音，再进入语音采集回合；按键仍可作为启动方式。
- `stand`（站立）动作已调整舵机映射和动作执行路径，并纳入固件主机测试。
- ESP32-S3 固件、Voice Gateway、ASR → OpenClaw → TTS 编排、联网搜索、OLED 表情及本地网页控制代码仍保留在同一项目链路中。

### 后续工作

- 完善 OpenClaw 的记忆层。
- 继续修复和验收动作的稳定触发、完成确认与异常恢复。
- 将当前通用本地唤醒模型替换为“芝麻”专用唤醒词。
- 自主设计并接入新的动作和表情资源。

硬件模块、GPIO 和供电关系见 [硬件模块清单](docs/hardware_modules.md)。
