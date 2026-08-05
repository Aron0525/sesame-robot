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

## v1.3.2 项目状态

完整的版本说明见 [v1.3.2 发布说明](docs/releases/v1.3.2.md)。以下只列当前事实，避免将代码存在误写为实机闭环已经完成。

### 已实现

- ESP32-S3 固件、Voice Gateway 和共享 JSON/音频契约已整理为单一运行链路；固件包含 I2S、Opus、WSS、网页控制、OLED、舵机和独立 NVS 配置。
- Gateway 已实现 ASR → OpenClaw → TTS 编排、受限 `response.plan`、中断处理、监控台和串口事件翻译。
- 基础聊天与可选联网搜索已实现；联网搜索默认关闭，仅 Gateway 可使用 DashScope，并对模型提出的查询、时效和结果进行边界校验。
- OLED 表情、可扩展表情资源、本地网页动作/舵机调试已实现；模型动作仅允许 `stop`、`rest`、`stand`、`wave`。

### 未完成或待实机验收

- 当前实机环境未发现 NVS 中配置的 2.4 GHz Wi-Fi，WSS 会话尚未在该环境建立。
- BOOT 录音到 ASR、OpenClaw、TTS Opus 播放、表情、模型动作及播放中断的完整 P2 硬件闭环待验收。
- 动作控制在真实语音闭环中的稳定触发、完成确认和异常恢复仍需调试；修正版 OLED 显示也待新的实机验证。
- 云端 ASR/TTS、OpenClaw 与联网搜索均需要本机配置凭据；音乐流媒体只有设计方案，未在本版本实现。

硬件模块、GPIO 和供电关系见 [硬件模块清单](docs/hardware_modules.md)。
