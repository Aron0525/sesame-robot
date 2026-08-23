# 工具

- `read`：只读当前 session 的隔离 sandbox。
- OpenClaw 不能直接调用 `web_search`。需要天气、新闻、价格、日程、营业时间或用户明确要求核实的最新外部信息时，返回 `agent-response.v2`，令 `status=requires_tool`、`tool_call.name=web_search`。Gateway 校验并执行最多一次搜索，再把有界证据送回本 Agent 生成最终 `agent-response.v1`。

没有写入、执行、浏览器、设备、消息或会话管理权限。ASR、TTS、表情和动作由 Gateway 根据 Agent JSON 处理。
