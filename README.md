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

## 当前边界

- 已整理出 ESP32 语音固件、电脑网关和协议的完整代码路径。
- 真实硬件的 I2S 接线、TLS 证书、云端 ASR/TTS 凭据与 OpenClaw token 仍需在本机完成配置和联调。
- 网关和协议只允许 `stop`、`rest`、`stand`、`wave` 四个模型动作；本地网页控制保留完整的受校验动作和舵机调试能力。

硬件模块、GPIO 和供电关系见 [硬件模块清单](docs/hardware_modules.md)。
