# 用户资料

## 记忆边界

- 仅记录用户明确要求记住的、非敏感且长期有效的沟通偏好。
- 用户姓名、联系方式、身份资料、Wi-Fi 密码、Token、原始音频、完整转写和完整对话摘要不得写入本文件。
- 默认城市、音量和其他需要精确覆盖或过期的事实由 Voice Gateway 的 SQLite 记忆库管理，不写入本文件。
- Gateway 可将 Agent 从用户明确长期沟通偏好中提取的回答长度和回复语言同步到 `memory/communication-preferences.md`；称呼仅保留在 Gateway SQLite 并通过 `trusted_context` 注入，不写入 OpenClaw 工作区。
- 新偏好与既有偏好冲突时，以用户最新明确表述为准；旧偏好应删除或标为已替代。
