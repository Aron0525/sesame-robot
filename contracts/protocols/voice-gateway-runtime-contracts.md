# Voice Gateway Runtime Contracts v1

所有跨模块边界都使用固定版本、拒绝未知字段、禁止传递文件内容或 Base64 Office 文档。

| 边界 | 输入 | 输出 | 校验者 |
|---|---|---|---|
| ESP32 → Gateway | `control-event.v1` JSON + 32-byte header 的一个 Opus 包 | `session.ready` / 错误 | Gateway |
| Opus Decoder → ASR | 固定 PCM S16LE、16 kHz、mono、20 ms frame | `AsrResult{text, confidence}` | Pipeline |
| Gateway → OpenClaw | `agent-request.v1` JSON，只有 conversation ID，不带原始 UserID/DeviceID | `agent-response.v1` JSON | OpenClaw Adapter + Policy |
| Agent → TTS | `reply.text`、允许的 `voice_id/style/speed` | 完整 PCM frame | Pipeline |
| Gateway → ESP32 | 一条 `response.plan`（白名单表情与至多一个动作）+ TTS Opus binary frame | `action.result` | ESP32 + Gateway |

## 固定失败规则

- 任何 JSON Schema、音频头、时序、ASR 输出或 Agent 输出验证失败：不进入后续阶段。
- Gateway 仅接受 `session.hello.sequence=0`；握手后的设备控制事件必须逐一严格递增。每个 `device_id` 只允许一个已认证的活跃 WebSocket 会话。
- Gateway 在接收路径即拒绝非 Opus、非 `stream_id=1`、变更 `generation_id`、超过 30 秒或超过字节上限的上行音频；不等待解码或 ASR 才发现超限。
- Agent 输出不合规：Gateway 只下发 `error{code:"agent_output_rejected"}`，不调用 TTS、不下发动作。
- OpenClaw session key 使用 `HMAC(secret, conversation_id)`，格式为 `agent:<agent_id>:conversation:<digest>`；不记录原始用户或设备标识。
- Sandbox 的物理删除只能由可信 Voice Gateway 以固定的 `openclaw sandbox recreate --session <derived-key> --force` 命令执行；模型没有该工具权限。
- 每一轮请求独占一个 Opus 编解码器实例。审计日志只能记录会话、轮次、包数、字节数、generation、动作或表情 ID 和状态；不得记录音频、识别文本、TTS 文本、token 或错误详情。
