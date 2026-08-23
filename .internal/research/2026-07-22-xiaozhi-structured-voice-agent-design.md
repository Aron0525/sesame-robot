# 调研：借鉴小智 AI 的 Sesame 结构化语音 Agent 设计

> 日期：2026-07-22
> 状态：完成
> 目标：设计 Opus 解码、ASR、OpenClaw、TTS、表情与机器人动作之间的数据结构

## 1. 结论

小智 AI 最值得借鉴的是“媒体流、会话事件、表现事件、设备工具调用”分离，而不是某个具体 JSON 字段。Sesame 应使用同一条 WSS 传 binary Opus 和 JSON control events；OpenClaw 输出自然语言文本，并通过 typed tools 触发动作。不要让模型生成一个混合回复、TTS 状态、表情和动作的巨型 JSON。

## 2. 小智 AI 的结构化方式

### 2.1 WebSocket 双平面

- binary message：双向 Opus。
- JSON message：`hello/listen/stt/tts/llm/mcp/abort` 等事件。[S1]
- `stt` 显示识别文字；`tts` 管理 start/sentence_start/stop；`llm.emotion` 更新表情。[S1]
- 设备动作不混入自然语言回复，采用 MCP JSON-RPC 2.0：`initialize`、`tools/list`、`tools/call`、result/error。[S2][S3]

### 2.2 可借鉴与不照搬

可借鉴：控制流和媒体流分离；设备能力有 schema；动作调用有 request id 和结果回执；高权限工具与 AI 工具分开。

不照搬：小智典型 60 ms Opus frame 不一定适合 Sesame 的低延迟目标；其 `llm` 事件主要是表情，不应作为完整 Agent 结果；session_id 不能代替用户鉴权；当前 Sesame 无需在 ESP32 内实现完整 MCP server。

## 3. Sesame 推荐架构

```text
ESP32 binary Opus
  → WSS Transport Adapter
  → Opus Decoder
  → PCM Ring Buffer
  → Streaming ASR
  → Turn Manager
  → OpenClaw Adapter
      ├── streaming reply text → TTS Coordinator
      └── typed tool calls → Robot Control Adapter
  → TTS PCM → Opus Encoder → ESP32
```

### 3.1 后端模块

| 模块 | 职责 |
|---|---|
| WSS Transport Adapter | 鉴权、binary/JSON 分流、重连、限流 |
| Codec Pipeline | Opus↔PCM，20 ms packet，ring buffer |
| ASR Session | partial/final、VAD endpoint、热词 |
| Turn Manager | turn 状态、打断、generation、超时 |
| OpenClaw Adapter | 文本/session 输入、文本流/tool call 输出 |
| TTS Coordinator | 分句、流式 TTS、取消、下行 Opus |
| Robot Control Adapter | tool schema、白名单、安全策略、设备 ack |
| Event Bus | 进程内有序事件；不承载每个音频 packet |

## 4. 统一事件信封

所有 JSON 控制事件使用统一 envelope：

```json
{
  "v": 1,
  "type": "asr.final",
  "session_id": "ses_01",
  "turn_id": "turn_09",
  "request_id": "req_42",
  "seq": 18,
  "timestamp_ms": 1784700000000,
  "payload": {}
}
```

字段规则：

- `session_id`：一次 WSS/语音会话。
- `turn_id`：一次用户发言到机器人回答。
- `request_id`：一个动作/tool call，供幂等与回执。
- `generation_id`：一轮 TTS 音频代际，打断时丢弃旧代际。
- `seq`：诊断和去重，不作为用户权限。

## 5. 事件类型

### 5.1 连接与能力

```json
{
  "v": 1,
  "type": "session.hello",
  "session_id": "ses_01",
  "payload": {
    "protocol_version": 1,
    "audio": {
      "codec": "opus",
      "sample_rate": 16000,
      "channels": 1,
      "frame_duration_ms": 20
    },
    "capabilities": {
      "expressions": ["default", "happy", "sad", "angry", "surprised", "thinking"],
      "actions": ["stop", "wave", "dance", "rest", "stand", "bow", "shrug"]
    }
  }
}
```

服务器必须用固件版本对应的服务端 capability policy 求交，不能直接信任设备声明。

### 5.2 ASR 内部事件

```json
{"type":"asr.partial","session_id":"ses_01","turn_id":"turn_09","payload":{"text":"帮我挥"}}
```

```json
{
  "v": 1,
  "type": "asr.final",
  "session_id": "ses_01",
  "turn_id": "turn_09",
  "payload": {
    "text": "帮我挥挥手",
    "language": "zh-CN",
    "confidence": 0.94
  }
}
```

只有 `asr.final` 进入 OpenClaw；partial 默认只用于字幕和低延迟预测。

### 5.3 TTS 生命周期

```json
{"type":"tts.start","session_id":"ses_01","turn_id":"turn_09","payload":{"generation_id":10}}
```

```json
{"type":"tts.segment","session_id":"ses_01","turn_id":"turn_09","payload":{"generation_id":10,"text":"当然可以。"}}
```

之后发送多个带同一 `generation_id` 的 binary Opus message，最后：

```json
{"type":"tts.stop","session_id":"ses_01","turn_id":"turn_09","payload":{"generation_id":10,"reason":"completed"}}
```

### 5.4 表情事件

```json
{
  "v": 1,
  "type": "expression.set",
  "session_id": "ses_01",
  "turn_id": "turn_09",
  "request_id": "expr_15",
  "payload": {
    "name": "happy",
    "mode": "hold",
    "ttl_ms": 2500
  }
}
```

表情是表现层事件，不要把它伪装成机器人动作，也不要让自由文本直接成为 face 名称。

### 5.5 动作调用与回执

OpenClaw 看到的 typed tool：

```json
{
  "name": "sesame.perform_action",
  "description": "执行一个安全的预定义机器人动作",
  "inputSchema": {
    "type": "object",
    "properties": {
      "action": {
        "type": "string",
        "enum": ["wave", "dance", "rest", "stand", "bow", "shrug"]
      },
      "duration_ms": {
        "type": "integer",
        "minimum": 100,
        "maximum": 5000
      }
    },
    "required": ["action"],
    "additionalProperties": false
  }
}
```

Control Adapter 发送给设备：

```json
{
  "v": 1,
  "type": "action.execute",
  "session_id": "ses_01",
  "turn_id": "turn_09",
  "request_id": "act_77",
  "payload": {
    "action": "wave",
    "duration_ms": 1200,
    "deadline_ms": 1784700003000
  }
}
```

设备必须回：

```json
{
  "v": 1,
  "type": "action.result",
  "session_id": "ses_01",
  "turn_id": "turn_09",
  "request_id": "act_77",
  "payload": {
    "status": "completed",
    "error_code": null
  }
}
```

状态枚举：`accepted/running/completed/rejected/interrupted/failed/expired`。HTTP 200 或 WS send success 只能表示 accepted，不能表示 completed。

## 6. OpenClaw 输入输出边界

输入 OpenClaw：用户身份上下文、session、`asr.final.text`、允许工具的 schema、经过最小化的设备状态。

禁止输入：ESP32 IP、Wi-Fi 凭据、token、网络拓扑、原始 PCM/Opus、内部堆栈。

输出分两条：

1. 自然语言文本流 → TTS Coordinator。
2. typed tool calls → Robot Control Adapter。

不推荐让 OpenClaw返回：

```json
{"reply":"...","tts":"...","face":"...","move":"...","raw_command":"..."}
```

这种巨型对象把不同生命周期绑在一起，难以流式输出、取消、重试和分别回执。

## 7. Sesame 当前能力与安全裁剪

现有固件真实支持 command：`forward/backward/left/right/rest/stand/wave/dance/swim/point/pushup/bow/cute/freaky/worm/shake/shrug/dead/crab/stop`。表情还包含 `happy/sad/angry/surprised/sleepy/love/excited/confused/thinking` 及 talk 变体。

MVP 不向 OpenClaw开放持续移动 `forward/backward/left/right`、低层 motor angle、settings、firmware update。先开放：`stop/wave/dance/rest/stand/bow/shrug` 和安全表情。

必须在 Control Adapter 校验：enum、TTL/deadline、request_id 幂等、动作串行、队列上限、速率限制、stop 优先、断线 watchdog、设备状态。现有固件未知 command 可能仍返回 200，故不能依赖端侧容错。

## 8. 状态机

```text
IDLE
 → LISTENING
 → ASR_FINAL
 → THINKING
 → RESPONDING
    ├─ TTS_STREAMING
    └─ ACTION_EXECUTING
 → IDLE

任意状态 + user_speaking
 → INTERRUPTING
 → cancel OpenClaw/TTS
 → flush old generation
 → LISTENING
```

TTS 和动作是并行子状态。打断默认停止 TTS；是否停止动作由动作策略决定，持续移动必须停止，一次安全表情可继续。

## 9. 实施建议

1. 先实现统一 envelope 与 session/turn/request/generation ID。
2. 完成 binary Opus → decoder → PCM → streaming ASR，验证 `asr.partial/final`。
3. 接 OpenClaw 文本流，但暂不开放动作。
4. 接 TTS 生命周期和 generation 打断。
5. 实现 `sesame.set_expression/get_status/stop/perform_action` typed tools。
6. 增加 action ack、watchdog、幂等、审计和物理安全测试。

## 10. 来源

- [S1 小智 WebSocket 协议](https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md)
- [S2 小智 MCP 协议](https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-protocol.md)
- [S3 小智 MCP IoT 使用](https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage.md)
- [小智 WebSocket 端侧实现](https://github.com/78/xiaozhi-esp32/blob/main/main/protocols/websocket_protocol.cc)
- [小智端侧消息分发](https://github.com/78/xiaozhi-esp32/blob/main/main/application.cc)
- [Sesame firmware main](https://github.com/dorianborian/sesame-robot/blob/main/firmware/sesame-firmware-main.ino)
