# OpenClaw 当前配置清单（脱敏）

更新日期：2026-08-18（America/Los_Angeles）。本文件记录本机 `~/.openclaw/openclaw.json` 的有效配置摘要，以及仓库中用于重建 Sesame OpenClaw 环境的全部配置文档。不会记录 token、API key、凭据或会话内容。

## 已执行的调整

- 已移除企业微信的 channel 配置与唯一的路由绑定。
- 已禁用 `wecom-openclaw-plugin`，并移除全局 `wecom_mcp` 工具白名单。
- Gateway 已重启，配置校验通过；当前没有任何 channel 或 Agent routing binding。
- OpenClaw 2026.7.1-1 不允许删除内置 `main` Agent。它没有绑定、没有业务角色，不能通过受支持的 CLI 删除；实际机器人 Agent 为下方四个 Sesame Agent。
- `sesame-scene` MCP 的项目路径保留为 `/Users/mac/Desktop/2/endpoint-gateway`。

## 本机运行配置

| 项目 | 当前值 |
| --- | --- |
| OpenClaw | `2026.7.1-1`，LaunchAgent，运行中 |
| Gateway | `127.0.0.1:18789`，`loopback`，token 鉴权，Tailscale 关闭 |
| 默认模型 | `deepseek/deepseek-v4-flash` |
| 并发 | 默认最多 4 个 Agent；子 Agent 最多 8 个 |
| Channels | 无 |
| Bindings | 无 |
| Web 搜索 | 启用；`parallel-free`；最多 3 条；8 秒超时；5 分钟缓存 |
| 已启用插件 | `codex`、`deepseek`、`parallel` |
| 企业微信插件 | 已禁用；未卸载本地插件文件 |

### Agent

| ID | 用途/状态 | Workspace | 模型 | 允许工具 |
| --- | --- | --- | --- | --- |
| `main` | OpenClaw 内置默认 Agent；无绑定，CLI 不支持删除 | `~/.openclaw/workspace` | 默认模型 | 默认策略 |
| `sesame` | 正常模式 | `~/.openclaw/workspace-sesame` | `deepseek/deepseek-v4-flash` | `read`、`web_search` |
| `sesame-learning` | 学习模式 | `~/.openclaw/workspace-sesame-learning` | 同上 | `read`、`web_search` |
| `sesame-children` | 儿童模式 | `~/.openclaw/workspace-sesame-children` | 同上 | `read`、`web_search` |
| `sesame-work` | 工作模式 | `~/.openclaw/workspace-sesame-work` | 同上 | `read`、`web_search` |

四个 Sesame Agent 均启用 memory search（provider 为 `none`），并使用 Docker sandbox：`mode=all`、session scope、workspace 不挂载、网络关闭、只读根文件系统、非 root 用户、capabilities 全部丢弃、1 CPU、512 MiB 内存。它们拒绝写文件、编辑、执行、进程、消息/会话/子 Agent、自动化、节点和媒体等工具组；elevated 关闭。

### 模型

DeepSeek Provider 使用 `https://api.deepseek.com` 与 OpenAI-compatible completions API。已配置模型：

- `deepseek-v4-flash`：推理，1,000,000 context window（四个 Sesame Agent 正在使用）。
- `deepseek-v4-pro`：推理，1,000,000 context window。
- `deepseek-chat`：非推理，131,072 context window。
- `deepseek-reasoner`：推理，131,072 context window。

API key 仅保存在本机认证配置，未写入本文件。

### MCP Server

| 名称 | 启动命令 / 工作目录 | 工具 | 状态 |
| --- | --- | --- | --- |
| `esp32-lab` | `/Users/mac/Documents/multi-agent/openclaw-esp32/.venv/bin/python -m bridge.mcp_server`；cwd 同项目 | `get_device_state`、`set_led` | `mcp doctor` 正常 |
| `sesame-scene` | `/usr/bin/python3 /Users/mac/Desktop/2/endpoint-gateway/src/sesame_endpoint_gateway/openclaw_scene_mcp.py`；cwd `/Users/mac/Desktop/2/endpoint-gateway` | `list_scenes`、`get_scene`、`select_scene` | 配置有效；运行 probe 仍需该路径存在且 Endpoint Gateway 在 `127.0.0.1:8788` 运行 |

`sesame-scene` 使用 `SESAME_DEFAULT_DEVICE_ID=sesame-v3-001`、`SESAME_SCENE_GATEWAY_URL=http://127.0.0.1:8788`，并需要 `SESAME_SCENE_CONTROL_TOKEN`；该 token 已脱敏。OpenClaw 提示该 token 当前是配置内的明文敏感值，后续应改为环境变量或 SecretRef。

## 仓库内的配置文档与模板

| 文件 | 作用 | 当前内容/边界 |
| --- | --- | --- |
| `ops/openclaw/README.md` | Sesame 本机 OpenClaw 部署说明 | 四个 Agent、Gateway/MCP 边界、安装/验证流程、安全限制与未完成项。 |
| `ops/openclaw/config/README.md` | 模板合并规则 | 明确模板不能直接覆盖 `~/.openclaw/openclaw.json`，特别是 `agents.list`。 |
| `ops/openclaw/config/agents.template.json` | 四个 Agent 的可移植配置片段 | `sesame`、`sesame-learning`、`sesame-children`、`sesame-work`；读与搜索工具；禁止写/执行/会话类工具。 |
| `ops/openclaw/config/sesame-scene-mcp.template.json` | 场景 MCP 可移植片段 | 以 `<V3_PROJECT_ROOT>` 占位，指定三项 scene 工具与本地 secret token。 |
| `ops/openclaw/scripts/install_workspaces.sh` | 安装 workspace 模板 | 复制到 `~/.openclaw`，同名目录存在时停止，不覆盖。 |
| `ops/openclaw/workspaces/shared/{IDENTITY.md,TOOLS.md,HEARTBEAT.md}` | 四个 Agent 共用身份、工具与 heartbeat 规则 | 项目版本化规则；本机记忆与用户资料不在仓库。 |
| `ops/openclaw/workspaces/sesame/{AGENTS.md,SOUL.md}` | 正常模式规则和人格 | 正常模式。 |
| `ops/openclaw/workspaces/sesame-learning/{AGENTS.md,SOUL.md}` | 学习模式规则和人格 | 结论、原因、一个下一步或问题。 |
| `ops/openclaw/workspaces/sesame-children/{AGENTS.md,SOUL.md}` | 儿童模式规则和人格 | 短句、故事问答、一次一个任务。 |
| `ops/openclaw/workspaces/sesame-work/{AGENTS.md,SOUL.md}` | 工作模式规则和人格 | 结论优先、待办、风险与下一步。 |

`lessons/0010-sesame-openclaw-config.html` 与 `lessons/0011-openclaw-agent-sandbox-privacy.html` 是历史教学材料，不是本机部署源；其中描述的 `main`/微信现状已经过时，不能用于恢复当前配置。

## 模板与本机状态的有意差异

1. 模板使用 `<V3_PROJECT_ROOT>` 和 `.venv/bin/python`，以便新机器重建；本机实际场景 MCP 使用 `/Users/mac/Desktop/2` 和 `/usr/bin/python3`，按当前机器保留。
2. 模板的 sandbox 是 agent scope + workspace 只读；本机四个 Sesame Agent 是更严格的 session scope + workspace 不挂载、Docker 网络关闭。
3. 模板只描述 Sesame 片段；本机全局配置还包含 Gateway、DeepSeek Provider、`esp32-lab` MCP 与非企业微信插件。

## 验证结果与待办

- `openclaw config validate`：通过。
- `openclaw gateway status`：运行中，连接探测通过。
- `openclaw agents bindings`：无绑定。
- `openclaw channels status --probe`：Gateway 可达，未配置 channel。
- `openclaw mcp doctor`：`esp32-lab` 与 `sesame-scene` 配置正常；后者有 token 明文存储警告。
- `sesame-scene` 端到端 probe 仍依赖 `/Users/mac/Desktop/2/endpoint-gateway` 存在且 Gateway 服务运行；本次未启动 Endpoint Gateway。
