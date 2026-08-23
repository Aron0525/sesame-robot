# Learning Record 0001 — Sesame V3 源码的第一阅读框架

## Context

用户希望从“不懂项目”进入能够阅读、维护并在未来复现 Desktop/2 的 Sesame Robot V3 工程，不满足于架构图层面的了解。

## Current understanding to retain

- 正式固件入口是 `firmware/esp32_voice_idf/main/app_main.cpp`，而不是旧 Arduino 示例。
- `VoiceController` 是 ESP32 语音回合的总调度器；核心概念是长期对象、FreeRTOS 任务、队列、session/turn/generation/sequence。
- Gateway 的 `DeviceSession` 代表一条在线 WSS 连接；`ConversationPipeline` 代表一次语音回合，两者不要混淆。
- 当前代码明确让 Agent 的物理 `actions` 为空，并在下发时再次固定为 `None`；本地网页控制是独立通道。
- 应优先按一轮人工按键语音追踪：`app_main` → `VoiceController` → Gateway `DeviceSession` → `ConversationPipeline` → 下行播放。

## Evidence

- `/Users/mac/Desktop/2/firmware/esp32_voice_idf/main/app_main.cpp`
- `/Users/mac/Desktop/2/firmware/esp32_voice_idf/components/sesame_voice/voice_controller.cpp`
- `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py`
- `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/pipeline.py`

## Next lesson candidate

逐段阅读 `VoiceController::process_control_json()`，把每一种 Gateway 下行控制帧和 ESP32 状态变化做成一张状态转移表；然后单独讲 `GatewayClient` 的 Wi-Fi、mDNS、TLS/WSS 建连。
