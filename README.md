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

## v1.5.0：可靠采集、连续对话与动作隔离

- **可靠语音采集**：手动、唤醒词和追问采集使用明确来源与统一会话状态；500 ms 预录音减少开头吞字，音频上行与唤醒推理彼此解耦。
- **连续对话**：TTS 回复后保留 3 秒追问窗口，并支持播放期间通过完整唤醒词打断；独立下行队列避免语音流阻塞控制事件。
- **动作隔离**：OpenClaw 只生成文字、音色和表情，不再下发物理动作；网页本地控制继续使用原有动作白名单。
- **诊断与训练采样**：监控台会标记断线中的失败回合；测试录音可限制为手动按键采集，并记录对应 ASR 文本。
- **仍需现场配置**：真实硬件的 I2S 接线、TLS 证书、云端 ASR/TTS 凭据与 OpenClaw token 需要按设备在本机完成配置和联调。

硬件模块、GPIO 和供电关系见 [硬件模块清单](docs/hardware_modules.md)。

## 项目说明与迁移文档

- [项目概览与当前边界](docs/project-overview.md)：功能、组件职责、OpenClaw 的位置与当前限制。
- [GitHub 范围与跨设备复现](docs/setup/github-scope-and-replication-guide.md)：仓库包含什么、不包含什么，以及换设备时应迁移哪些内容。
- [换电脑后的搭建指南](docs/setup/new-machine-setup-guide.md)：写给开发者的本地搭建顺序。
- [给 AI 的新电脑搭建交接文档](docs/setup/ai-agent-new-machine-handoff.md)：在新电脑交给 AI 执行配置时使用。
