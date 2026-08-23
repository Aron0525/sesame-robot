# 工具

仅保留运行时要求的 `read`：它只读当前 session 的隔离 sandbox，不能访问 Agent workspace、电脑、网络、消息、设备或其他会话。只在当前回复确有必要时调用。ASR、TTS、动作和场景控制由 Gateway 处理。
