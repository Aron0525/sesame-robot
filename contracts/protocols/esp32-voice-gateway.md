# ESP32-S3 ↔ Sesame Voice Gateway

## Transport

- 服务类型：`_sesame-gw._tcp.local.`
- 接口路径：`/v1/device-stream`
- 开发环境：可信局域网中的 WS
- 生产环境：WSS + 设备鉴权
- 一台设备维持一条全双工长连接

## 握手

1. ESP32-S3 通过 mDNS 发现 Voice Gateway。
2. 校验 `gateway_id`、协议版本和 TLS 身份。
3. 建立 WebSocket。
4. WebSocket Upgrade 携带 `Authorization: Bearer <device_token>`，连接后发送 `session.hello`。
5. Voice Gateway 验证配对关系。
6. Voice Gateway 返回 `session.ready`、服务器签发或恢复的 `conversation_id` 和音频参数。
7. 双方才允许传输音频。

当前开发骨架使用每台设备独立的 bearer token。生产版仍需将 token 引导过程升级为首次配对，并把 `gateway_id` 绑定到已固定的 TLS 公钥。

`session_id` 只标识本次 WebSocket 连接；`conversation_id` 标识一段可恢复的对话。设备首次连接在 `session.hello.payload.conversation_id` 发送 `null`，Gateway 返回新 ID；同一设备在 TTL 内重连时带回该 ID。Gateway 只接受与已认证 DeviceID/UserID 匹配的 ID。已认证设备携带的 ID 若因网关重启或 TTL 过期而未知，Gateway 必须签发新 ID；归属不匹配仍必须拒绝。

同一 `device_id` 在任意时刻只能有一个已认证 WebSocket 会话。后来的连接必须被拒绝，而不能与旧连接同时下发动作或语音。

## 控制事件

JSON text frame 使用固定信封：

```json
{
  "v": 1,
  "type": "listen.start",
  "session_id": "ses_001",
  "turn_id": "turn_001",
  "request_id": null,
  "sequence": 1,
  "timestamp_ms": 1000,
  "payload": {}
}
```

设备上行事件：

```text
session.hello
listen.start
listen.stop
interrupt
action.result
```

服务下行事件：

```text
session.ready
response.plan
tts.start
tts.stop
tts.flush
error
```

### Control sequence

- `session.hello` 的 `session_id`、`turn_id`、`request_id` 必须为 `null`，且 `sequence` 必须为 `0`。
- Gateway 发送 `session.ready` 后，设备的每个控制事件必须从 `1` 开始严格递增，不允许重复、跳号或回退。
- 两端各自维护自己的发送序号；重新建立 WebSocket 时重置序号，不跨会话延续。
- `session_id`、`turn_id`、`request_id` 和 `conversation_id` 都是机器生成标识符，只能使用 ASCII 字母、数字、`_`、`-`；不得把文本或语音内容放入这些字段。

## 音频

首版固定：

```text
Opus VOIP
16000 Hz
mono
PCM S16LE
20 ms
320 samples / 640 PCM bytes
```

一条 WebSocket binary message：

```text
[固定包头][一个完整 Opus packet]
```

首版包头固定为 32 字节，使用网络字节序（big-endian）：

```text
偏移  大小  类型     字段
0     4     bytes    magic = "SSM1"
4     1     uint8    protocol_version = 1
5     1     uint8    direction：0=ESP32→电脑，1=电脑→ESP32
6     2     uint16   flags
8     4     uint32   stream_id
12    4     uint32   generation_id
16    4     uint32   sequence
20    8     uint64   timestamp_ms
28    4     uint32   payload_length
32    N     bytes    一个完整 Opus packet
```

Python `struct` 格式为 `!4sBBHIIIQI`。单个 Opus payload 上限为 1500 字节。接收方必须校验 magic、版本、方向、连续 sequence 和 payload 长度。

生产上行只接受 `direction=0`、`flags=0`（Opus）、`stream_id=1` 和非零且整轮一致的 `generation_id`。一轮最多 1500 包（30 秒 × 每秒 50 包），最多 2,250,000 个 Opus payload 字节；超过任一限制时 Gateway 终止该会话，不会把音频交给 ASR。


## 回复计划

```json
{
  "v": 1,
  "type": "response.plan",
  "session_id": "ses_001",
  "turn_id": "turn_001",
  "request_id": null,
  "sequence": 20,
  "timestamp_ms": 3000,
  "payload": {
    "generation_id": 7,
    "expression_id": "happy",
    "expression_ttl_ms": 2500,
    "action_id": "wave",
    "action_request_id": "act_001",
    "action_duration_ms": 1200,
    "action_deadline_ms": 8000
  }
}
```

`action_id` 可以为 `null`，此时三个 `action_*` 字段也必须为 `null`。ESP32-S3 必须拒绝未知动作、过期请求、越界参数和不安全状态；任一字段非法时整条计划不执行，并在接受动作后返回 `action.result`。

`action_deadline_ms` 与该控制帧顶层 `timestamp_ms` 使用同一网关时钟。ESP32 只
使用两者的差值换算为本地截止时间，因此不依赖电脑与 ESP32 的系统时钟同步。
