# OpenClaw 配置模板使用说明

本目录的 JSON 文件都是**配置片段**，用于让人或 AI 对照本机 `~/.openclaw/openclaw.json` 合并，不是可直接覆盖全局配置的完整文件。

`openclaw.json` 往往同时保存个人渠道、模型 Provider、其他 Agent、MCP、认证方式和本机路径。尤其是 `agents.list` 为数组，直接对模板运行 `openclaw config patch --file ...` 会替换数组并删除不属于 Sesame 的 Agent。因此合并时只创建或更新 ID 为 `sesame`、`sesame-learning`、`sesame-children`、`sesame-work` 的条目。

`sesame-scene-mcp.template.json` 的路径和 token 都是占位符。先替换项目根路径，再以本机 SecretRef 或服务环境变量提供 `SESAME_SCENE_CONTROL_TOKEN`。严禁将已填好的实际配置文件放回本目录。

完成合并后依次执行：

```bash
openclaw config validate
openclaw agents list
openclaw mcp doctor
openclaw mcp probe sesame-scene
```

若配置 schema 与模板不一致，先运行 `openclaw config schema` 和 `openclaw config set --help`，按当前版本调整。不要通过放开 sandbox、增加 `exec` 权限或暴露全部 MCP 工具来绕过验证错误。
