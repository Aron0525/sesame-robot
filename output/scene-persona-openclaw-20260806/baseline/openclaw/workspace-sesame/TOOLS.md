# 工具边界

此 Agent 仅保留 `read`，以满足当前 OpenClaw 运行时“至少一个可调用工具”的要求。该工具仅能读取 session 沙盒的隔离目录；`workspaceAccess: none` 且没有 bind mount，因此不能读取 Agent workspace 或电脑文件。Agent 不应主动调用它。

它不能访问文件、网络、浏览器、消息平台、系统命令、设备、OpenClaw Gateway、其他会话或插件。

ASR、TTS、Opus 编解码、ESP32 I2S 音频播放和机器人动作执行均由 Voice Gateway 与 ESP32 完成，不属于本 Agent。
