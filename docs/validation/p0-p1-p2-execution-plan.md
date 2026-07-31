# Sesame V3 P0–P2 实机验证计划

交付目录：`/Users/mac/Desktop/1/SesameV3_语音机器人项目`。

## P0：隔离启动诊断

烧录 `firmware/esp32_voice_idf/diagnostics/p0_boot_diagnostic`。该工程只使用串口日志与 FreeRTOS 心跳，明确不初始化 PSRAM、Wi-Fi、I2S、OLED、舵机或网关。验收证据是完整启动日志及 `P0_PASS` 前连续 60 个 `P0_HEARTBEAT`，中间无复位。

## P1：完整固件 WSS 会话

以真实硬件的 16MB Flash 配置构建完整 ESP-IDF 固件；网页、OLED 与舵机必须独立于语音网关启动。设备从 NVS 读取 Wi-Fi、设备身份、令牌和根 CA，经 mDNS 发现 Voice Gateway，并完成 `session.hello` / `session.ready`。验收证据包含 ESP32 与网关两侧日志。

## P2：BOOT 受控对话

按一次 BOOT 开始录音、再按一次结束。网关必须按单一 `response.plan` 下发白名单 `expression_id` 与可选 `action_id`，再以 Opus 发送 TTS 音频。第二个按键在播放时必须记录 `interrupt`、停止功放及丢弃旧 generation。验收证据覆盖 ASR、OpenClaw、TTS、`response.plan`、Opus 播放和打断；云端模型调用只在已配置的端侧密钥和显式同意下进行。
