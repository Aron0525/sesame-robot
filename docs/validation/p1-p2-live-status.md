# P1 / P2 实机状态记录

日期：2026-07-28

## 已完成的实机准备

- 已从新的 `build-p1` 烧录完整 ESP32-S3 固件，确认启动配置为 16 MB Flash、DIO、80 MHz 和 8 MB OPI PSRAM。
- 私密 NVS 已单独写入 `0x9000`；源码、日志和本文档均不包含 Wi-Fi 密码、设备 token 或证书内容。
- 本机 TLS/WSS Voice Gateway 已成功启动；根 CA 校验的 HTTPS `/healthz` 返回正常。
- 网关单元测试 35/35 通过；Arduino 网页控制兼容草图也已用 ESP32-S3 FQBN 编译通过。

## P1 当前实测结果

完整固件启动后，舵机服务、网页 HTTP 服务、I2S 音频和语音任务均已启动，PSRAM 内存测试通过。

WSS 会话尚未建立，原因已通过 ESP32 Wi-Fi 驱动的断连事件定位：

- `WIFI_EVENT_STA_DISCONNECTED` 的原因码为 `201`（`WIFI_REASON_NO_AP_FOUND`）。
- 这表示 ESP32 在当前物理位置没有扫描到 NVS 中配置的 2.4 GHz Wi-Fi 网络；它不是 token、TLS、mDNS、OpenClaw 或密码校验阶段的失败。

因此 P1 的 `session.hello → session.ready` 和 P2 的 BOOT 对话不能在当前无线环境下诚实验收。

## 恢复验收所需外部条件

让 ESP32-S3 能接入与本机网关相同 LAN 的可见 2.4 GHz Wi-Fi（SSID 不隐藏，信号可达；5 GHz-only 网络不适用于 ESP32-S3）。网络可见后，设备会继续自动重试，无需重新写入 NVS；如改用其他网络，需仅重新生成并写入私密 NVS。

## P2 验收序列（P1 成功后）

1. 串口确认 `P1 session.hello sent`、`P1 session.ready accepted`，网关确认 `device_session_authenticated` 与 `session_ready_sent`。
2. 用户按一次 BOOT 开始录音、说一句短话、再按一次 BOOT 结束。
3. 验证网关中的 ASR、OpenClaw、`response.plan`、TTS 和 Opus 帧审计记录，以及 ESP32 的播放、表情和动作日志。
4. 播放期间再按一次 BOOT，验证 `interrupt` / `tts.flush`、旧 generation 丢弃和动作急停记录。
