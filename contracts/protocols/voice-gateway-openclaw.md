# Sesame Voice Gateway ↔ OpenClaw Gateway

## 连接

OpenClaw 与 Voice Gateway 运行在同一台电脑：

```text
ws://127.0.0.1:18789
```

Voice Gateway 通过 OpenClaw Adapter 完成：

- `connect` 握手；
- token、角色、scope 和协议版本验证；
- Agent run 启动；
- 流式事件接收；
- 一个总 deadline 内的有限重连；
- 超时/打断时对特定 `runId` 的 `chat.abort`；
- 输出归一化。

OpenClaw Gateway 保持 loopback 绑定，不向 ESP32-S3 或局域网暴露。

## 稳定内部请求

```json
{
  "v": 1,
  "request_id": "req_001",
  "conversation_id": "conv_001",
  "turn_id": "turn_001",
  "input": {
    "type": "text",
    "text": "和我挥手打招呼"
  },
  "capabilities": {
    "actions": ["wave", "stand", "rest", "stop"],
    "expressions": ["idle", "happy", "sad", "angry", "surprised", "sleepy", "love", "excited", "confused", "thinking"]
  }
}
```

## 稳定内部响应

```json
{
  "v": 1,
  "request_id": "req_001",
  "turn_id": "turn_001",
  "status": "completed",
  "reply": {
    "text": "你好，很高兴见到你。"
  },
  "voice": {
    "voice_id": "sesame_default",
    "style": "happy",
    "speed": 1.0
  },
  "expression": {
    "name": "happy",
    "ttl_ms": 2500
  },
  "actions": [
    {
      "name": "wave",
      "duration_ms": 1200
    }
  ]
}
```

OpenClaw 原生事件可能随锁定版本变化，Adapter 必须把它们映射为项目内部固定 Schema。具体 OpenClaw RPC method 只有在锁定版本后才能写入实现。

## 受控联网搜索

当 Voice Gateway 配置了联网搜索，OpenClaw 可以在第一轮返回 `agent-response.v2` 的
`requires_tool` 请求，且只允许 `web_search(query, freshness_days?)`；`freshness_days` 只能是
`7`、`30`、`180` 或 `365`。Gateway 校验参数、
执行一次 DashScope 搜索，再将来源受限的结果作为不可信证据送回 OpenClaw。第二轮必须返回
原有的 `agent-response.v1` `completed` 结果；不允许再次请求工具。OpenClaw 不获得搜索 API Key、
任意 URL 访问或网页抓取权限。

## 数据边界

允许发送：

- ASR 文本；
- Gateway 派生的 `conversation_id`、`turn_id` 和请求关联 ID；
- 允许的动作和表情能力；
- 最少量必要上下文。

禁止发送：

- Opus 或 PCM；
- WiFi 密码；
- 设备私钥；
- Voice Gateway token；
- 原始 `device_id`、`user_id`、设备会话 ID；
- 不必要的用户文件和路径；
- 未经用户授权的长期音频记录。
