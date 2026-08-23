# Research: ESP32 双向 Opus、OpenClaw 与用户隐私沙盒

> **Date:** 2026-07-21
> **Status:** Complete

## Summary

ESP32 的上行麦克风与下行 TTS 都应按短 Opus packet 流式处理，但 OpenClaw 不应位于实时音频数据面。OpenClaw 仅在需要工具调用、记忆和任务编排时加入；其内置 sandbox 只隔离 Agent 工具执行，多用户隔离仍需每个信任边界独立 Gateway/容器或 VM。

## Key Findings

### 双向音频都是流式

> **Confidence:** high — RFC 与 Espressif 官方资料一致。

- 推荐 16 kHz mono、20 ms 一个 Opus packet，一个 packet 对应一个 binary WebSocket message。[S1][S2]
- WebSocket/TCP 是长连接，但不要把整轮讲话做成无限 message；ESP32 需要处理 WebSocket fragmentation。[S1][S3]
- 上下行都采用有界队列：I2S DMA、PCM ring buffer、Opus codec、WebSocket task 分离。[S4]
- MVP 先做半双工；全双工打断需要 AEC，并以 generation_id 显式取消和清空旧 TTS。[S5]

### OpenClaw 是可选的控制面

> **Confidence:** high — 职责边界由项目需求决定。

OpenClaw 不负责 Opus 传输、ASR 或 TTS 实时调度。仅当产品需要工具调用、长期记忆、skills、多步骤任务或多渠道接入时才有价值。纯语音问答使用普通后端编排更简单。

### 沙盒必须分两层理解

> **Confidence:** high — OpenClaw 官方明确区分工具 sandbox 与租户边界。

- 内层：OpenClaw sandbox 隔离 shell/files/browser 等 Agent 工具执行；建议 `mode: all`、`scope: session`、`workspaceAccess: none`、关闭 elevated、工具白名单。[S6][S7]
- 外层：多用户产品按租户运行独立 Gateway、凭据、数据卷、网络与容器/VM。一个共享 Gateway 加 per-session sandbox 不是敌对多租户安全边界。[S8]
- Gateway 默认只监听 loopback，并由业务后端完成设备身份、用户授权、速率限制、审计和数据生命周期。[S7]

## Comparisons

| 方案 | 适用情况 | 判断 |
|---|---|---|
| 不用 OpenClaw | 单轮 ASR→LLM→TTS | 首版推荐 |
| 单 Gateway + 内置 sandbox | 单用户/单家庭可信边界 | 可用 |
| 每租户 Gateway + 内置 sandbox | 多用户产品 | 推荐 |
| 单 Gateway + session sandbox | 互不信任多用户 | 不足 |

## Codebase Context

- `firmware/sesame-firmware-main.ino` 当前是同步 HTTP WebServer 和单 loop，不含 WebSocket/I2S 音频任务。
- V3 使用 ESP32-S3-WROOM-1-N16R8，但现有排针只确认 3 个空闲 GPIO，标准全双工 I2S 接线存在硬件阻塞。
- 需要新增 I2S DMA、Opus、WebSocket、ring buffer、有界队列、断线恢复和打断状态机。

## Recommendations

1. 音频服务独立于 OpenClaw，先跑通半双工语音闭环。
2. OpenClaw 通过窄业务 tool 返回结构化动作，不直接接触音频流或机器人任意命令。
3. 单用户先用 OpenClaw 内置 sandbox；多用户增加每租户独立 Gateway 容器/VM，内置 sandbox 作为第二层。
4. 开工前确认 I2S 麦克风、DAC/功放、四根信号引脚、电源噪声与是否改 PCB。

## Open Questions

- 麦克风、DAC/功放型号与 I2S 引脚分配。
- 是否要求边播边听/AEC，还是 MVP 半双工。
- 产品是单设备可信用户还是 SaaS 多租户。

## Sources

- [S1 RFC 6455 WebSocket](https://www.rfc-editor.org/rfc/rfc6455.html) — Primary
- [S2 RFC 6716 Opus](https://www.rfc-editor.org/rfc/rfc6716.html) — Primary
- [S3 Espressif WebSocket Client](https://github.com/espressif/esp-protocols/blob/master/components/esp_websocket_client/include/esp_websocket_client.h) — Primary
- [S4 ESP-ADF Audio Pipeline](https://docs.espressif.com/projects/esp-adf/en/latest/api-reference/framework/audio_pipeline.html) — Primary
- [S5 ESP-SR AFE/AEC](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/audio_front_end/README.html) — Primary
- [S6 OpenClaw Sandboxing](https://docs.openclaw.ai/gateway/sandboxing) — Primary
- [S7 OpenClaw Security](https://docs.openclaw.ai/gateway/security) — Primary
- [S8 OpenClaw Multi-tenant Hosting](https://docs.openclaw.ai/gateway/multi-tenant-hosting) — Primary
