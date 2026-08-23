# JSON 输入输出契约

## 基本规则

- 所有 JSON 必须包含整数版本 `v`。
- 未知字段默认拒绝。
- 所有字符串、数组和数值设置上限。
- 动作、表情、状态使用 enum 白名单。
- 不兼容版本明确拒绝，不能静默猜测。
- `session_id` 只用于路由，不用于鉴权。
- `conversation_id` 是 Gateway 签发的短期对话标识；只能由原认证设备在 TTL 内恢复。
- Gateway → OpenClaw 只传 `conversation_id`，不得传递原始 UserID、DeviceID、文件内容或音频。
- Opus 使用 binary frame，不使用 JSON 或 Base64。

## 控制事件信封

```json
{
  "v": 1,
  "type": "listen.start",
  "session_id": "ses_001",
  "turn_id": "turn_001",
  "request_id": null,
  "sequence": 1,
  "timestamp_ms": 1000,
  "payload": { "trigger": "manual" }
}
```

Schema：

- [`control-event.v1.schema.json`](../schemas/control-event.v1.schema.json)
- [`agent-request.v1.schema.json`](../schemas/agent-request.v1.schema.json)
- [`agent-response.v1.schema.json`](../schemas/agent-response.v1.schema.json)

## Agent 输入

Gateway 发送给 OpenClaw 的内容必须是整个 `agent-request.v1` 对象，不加 Markdown、前缀或其他字段：

```json
{
  "v": 1,
  "request_id": "req_001",
  "conversation_id": "conv_001",
  "turn_id": "turn_001",
  "input": { "type": "text", "text": "请挥手" },
  "capabilities": {
    "actions": ["stop", "wave", "rest", "stand"],
    "expressions": ["idle", "happy", "sad", "angry", "surprised", "sleepy", "love", "excited", "confused", "thinking"],
    "voices": ["sesame_default"]
  }
}
```

## Agent 输出

OpenClaw 必须只返回 `agent-response.v1` JSON 对象。Gateway 逐项校验 request/turn 关联、动作、表情、音色、风格和速度；任一项失败只发送 `error`，不会调用 TTS 或下发动作。

## Gateway 下行回复计划

Gateway 不把 ASR 文本或模型回复文本发送回 ESP32。它先发送一个 `response.plan`：其中只能包含一个白名单表情和至多一个白名单动作，随后以 binary Opus 帧发送已合成的语音。模型动作白名单严格限于 `stop`、`wave`、`rest`、`stand`，不能发送八路舵机角度；完整字段与空动作规则见 [`esp32-voice-gateway.md`](./esp32-voice-gateway.md)。

## 错误事件

```json
{
  "v": 1,
  "type": "error",
  "session_id": "ses_001",
  "turn_id": "turn_001",
  "request_id": "req_001",
  "sequence": 30,
  "timestamp_ms": 4000,
  "payload": {
    "code": "ASR_TIMEOUT",
    "message": "ASR did not return before the deadline",
    "retryable": true
  }
}
```

错误码必须稳定，`message` 只用于诊断，不作为程序分支条件。

## 版本策略

- 增加可选字段：保持 `v: 1`，但旧实现必须能够明确处理或拒绝。
- 改变字段含义、类型或必填条件：发布 `v: 2`。
- Voice Gateway 在 `session.ready` 中确认最终协议版本。
- 未知版本返回 `UNSUPPORTED_PROTOCOL_VERSION`。
