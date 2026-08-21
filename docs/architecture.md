# 当前架构与联调边界

## 数据流

```text
按下 ESP32-S3 的 BOOT 键
  → INMP441 采集 16 kHz PCM
  → Opus（20 ms）通过 WSS 发送到 Voice Gateway
  → ASR 产生文本
  → OpenClaw / LLM 返回受限 JSON：回复文本、表情、动作
  → TTS 产生 16 kHz PCM
  → Gateway 重新编码为 Opus，通过 WSS 下发
  → MAX98357A 播放；动作白名单驱动舵机
```

## 责任边界

- ESP32：音频采集/播放、网络连接、协议校验、动作二次校验；不处理 LLM，也不保存密钥到源码。
- Voice Gateway：唯一编排器，负责 Opus、ASR、OpenClaw、TTS 和设备会话。
- OpenClaw：只接收 ASR 文本和允许的能力列表；不能收到原始音频、Wi-Fi 密码或设备 token。

## 硬件固定引脚

| 用途 | GPIO |
| --- | --- |
| INMP441 I2S0 BCLK / WS / 数据 | 14 / 47 / 48 |
| Hat SPK2 I2S1 BCLK / LRCK / SDATA | 1 / 2 / 3 |
| 舵机 S0–S7 | 4, 5, 6, 7, 10, 11, 12, 13 |

Hat SPK2 以 3V3 和公共 GND 供电；两条 I2S 总线不共享 BCLK 或 WS。语音固件要求
ESP32 与舵机电源共地，八路舵机使用独立 5–6 V 电源。I2S 硬件和网关 TLS 未完成实体
联调前，不应声明语音闭环已可用。
