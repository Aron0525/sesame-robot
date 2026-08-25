# 2026-08-21 ESP32 网关恢复记录

## 结论

ESP32 已恢复连接电脑 Gateway。实时会话快照显示设备 `sesame-stream-lab-001` 为 `online=true`、`current_stage=ready`。

本次故障的直接原因不是路由器或 2.4/5 GHz 频段，而是正式 Gateway 的 macOS LaunchAgent 标签 `com.sesame.streaming-lab-gateway` 处于 `disabled`。手动启动的进程不能作为常驻服务，退出后 8766 无监听，ESP 只能持续执行 mDNS 重试。

## 当前匹配版本

- Gateway：0821 streaming-lab 快照。
- ESP32：同一 0821 快照构建的应用分区。
- mDNS：`_sesame-streamgw._tcp.local.`。
- WSS：TLS、端口 8766、入口 `/v2/device-stream`。
- TLS 主机名：`sesame-stream-gateway.local`。
- 设备：`sesame-stream-lab-001`。

不要把上述 ESP 固件与 v1.6 默认的 `_sesame-gw._tcp.local.`、8765、`/v1/device-stream` Gateway 混用。

## 已执行验证

1. 0821 Gateway 离线测试：129 项通过。
2. 0821 ESP32 应用镜像构建成功，二进制包含 `_sesame-streamgw` 和 `/v2/device-stream`。
3. 只写入应用分区 `0x10000`，写入后 hash 校验通过；NVS 未擦除。
4. ESP 串口确认连接 `YuanGuang`，取得 `192.168.88.183`。
5. 正式 LaunchAgent 启用后，8766 由 Gateway 持续监听。
6. Gateway 快照确认 WSS connected 和 session ready。
7. 电脑 ping ESP 无丢包，`http://192.168.88.183/` 返回 HTTP 200。
8. 本机控制台域名解析到 `192.168.88.98`，`https://sesame-stream-gateway.local:8766/console` 返回 HTTP 200。

## 常驻服务

正式标签：

```text
com.sesame.streaming-lab-gateway
```

旧的重复标签 `com.sesame.voice-gateway` 已禁用，避免两个 Gateway 争用端口或广播不同的 mDNS 服务。

检查命令见[电脑端网关运行说明](../run-gateway.md)。

## ASR → OpenClaw → TTS → ESP32 实机复测

连接恢复后首次真实回合在 OpenClaw 阶段失败。OpenClaw 日志明确记录
`token_mismatch`：Gateway `.env` 中保存的是 64 字符旧 token，而当前
`~/.openclaw/openclaw.json` 使用 48 字符 token。修复后，直接 Provider 在启动时
读取本机 OpenClaw 当前 token，不打印或复制秘密；回归测试覆盖旧副本与当前 token
不一致的场景。

修复后的真实回合 `turn_52be0209_4` 完成：

- INMP441 / Opus 上行：366 包，7.32 秒。
- ASR：1.120 秒，成功识别 27 字文本。
- OpenClaw：2.393 秒，成功返回 39 字回复。
- TTS：1.006 秒，生成 375 帧、240000 字节 PCM。
- Opus 编码：0.518 秒，375 包。
- Gateway 下发：375/375 包，ESP32 会话回到 `online=true`、`ready`。

该回合同时暴露第二个问题：7.5 秒音频曾被拖到 54.499 秒。根因是当前 ESP32
不发布可选 `playback.stats`，而 Gateway 在每个后续帧前重复等待 150 ms。修复为
只等待一次遥测后回退到绝对 20 ms 节拍。最终复检结果：

- 3 秒 / 150 帧：实际节拍 2.981 秒，期望 2.980 秒，累计漂移 1 ms。
- 服务最终重启后的 2 秒 / 100 帧：实际节拍 1.983 秒，期望 1.980 秒，累计漂移 3 ms。
- 两次测试后 ESP32 均保持 `online=true`、`ready`。
- 0821 Gateway 测试：131 项通过。

USB 串口节点在最后复检时已从 macOS `/dev` 消失，因此最终播放完成没有使用串口
日志作为证据；本次结论基于真实上行回合、Gateway 各阶段事件、完整 WSS 下发计数、
下发节拍及 ESP32 会话状态。串口恢复后仍可补充扬声器完成日志验收。
