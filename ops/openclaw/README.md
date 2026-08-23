# Sesame Robot V3 的本地 OpenClaw 配置

本目录保存 Sesame Robot V3 需要的 OpenClaw **项目配置模板**，用于在新电脑上重建四个 Sesame Agent 和 `sesame-scene` MCP bridge。OpenClaw 运行在本地电脑，不运行在 ESP32，也不属于原版 `dorianborian/sesame-robot` GitHub 仓库。

已在本机验证的 OpenClaw 版本是 `2026.7.1-1`。新电脑优先使用相同版本；如果使用更高版本，先执行本文的验证命令，遇到配置字段变化时以 [OpenClaw 配置 schema](https://docs.openclaw.ai/cli/config) 为准，不要静默改写安全策略。

## 这里包含和不包含的内容

| 包含 | 用途 |
| --- | --- |
| `workspaces/` | `sesame`、`sesame-learning`、`sesame-children`、`sesame-work` 四个 Agent 的公共规则和场景规则。 |
| `config/agents.template.json` | 四个 Agent 应有的 workspace、只读工具和 sandbox 配置参考。 |
| `config/sesame-scene-mcp.template.json` | OpenClaw 启动本项目 MCP bridge 的配置参考。 |
| `scripts/install_workspaces.sh` | 安全复制 workspace 模板到本地 OpenClaw 状态目录，不覆盖已有 workspace。 |

以下内容刻意不在这里，也不能提交到 Git：

- `~/.openclaw/openclaw.json`：全局配置，可能包含模型、渠道和本机路径；
- `~/.openclaw/agents/*/agent/auth-profiles.json`、`credentials/`、`identity/`：模型和设备凭据；
- `~/.openclaw/agents/*/sessions/`、`sandboxes/`：会话和运行时状态；
- 每个 workspace 的 `USER.md`、`MEMORY.md` 和 `memory/`：用户资料和长期记忆；
- `SESAME_SCENE_CONTROL_TOKEN`、设备 token、Wi-Fi 密码、私有 CA 证书。

OpenClaw 官方也把 workspace 与 `~/.openclaw/` 的配置、凭据和会话明确分开；workspace 可以在私有仓库备份，但凭据和会话不应提交。[Agent workspace 文档](https://docs.openclaw.ai/agent-workspace)

## 架构边界

```text
ESP32-S3 ── WSS/Opus ── Endpoint Gateway ── ASR / Agent Adapter ── OpenClaw
                                 │                                    │
                                 └── 本机控制台             sesame-scene MCP bridge
                                                                  │
                                                         仅读取/切换场景
```

OpenClaw 只处理文本、会话、记忆和被允许的工具调用。它不能直接获得 ESP32 的 IP、设备 token、舵机权限或任意宿主机命令。Gateway 继续负责设备身份验证、动作/表情白名单和向设备转发受控消息。

当前 `sesame-scene` MCP server 只暴露 `list_scenes`、`get_scene`、`select_scene`。它不包含动作控制工具。

## 新电脑部署顺序

### 1. 安装并初始化 OpenClaw

按 [OpenClaw 安装文档](https://docs.openclaw.ai/install) 在新电脑安装指定版本。官方 macOS/Linux 安装器会下载并执行远程脚本；执行前由操作者确认来源和版本。安装后先建立基线：

```bash
openclaw --version
openclaw setup --baseline
openclaw config validate
```

不要直接从旧电脑复制整个 `~/.openclaw/`。那样会把身份、凭据、会话和 sandbox 一起迁走，既不安全，也会把旧机器的状态误当作项目配置。

### 2. 先准备并启动 Endpoint Gateway

MCP bridge 会调用 Gateway 的 `/v1/openclaw/*` API，所以 Gateway 必须先启动。进入项目目录后：

```bash
cd <V3 项目目录>/endpoint-gateway
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e ".[test]"

export SESAME_DEVICE_ID='sesame-v3-001'
export SESAME_DEVICE_TOKEN='<本机开发或真实设备的 token>'
export SESAME_GATEWAY_ID='sesame-edge'
export SESAME_SCENE_CONTROL_TOKEN='<新生成的本机控制 token>'

python -m uvicorn --app-dir src \
  sesame_endpoint_gateway.server:create_runtime_app \
  --factory --host 127.0.0.1 --port 8788
```

新电脑的 `SESAME_SCENE_CONTROL_TOKEN` 必须同时提供给 Gateway 与 OpenClaw 的 `sesame-scene` MCP 进程；两边不一致时，场景 API 会返回未授权。不要把该 token 放入仓库内的模板或网页。

### 3. 安装四个 workspace 模板

在另一个终端执行：

```bash
cd <V3 项目目录>
bash ops/openclaw/scripts/install_workspaces.sh
```

脚本默认写入 `~/.openclaw/`。若 OpenClaw 使用非默认状态目录，可先设定 `OPENCLAW_HOME`。脚本遇到同名 `workspace-sesame*` 目录会停止，不会覆盖现有工作区；需要更新时，先比较模板和现有文件，再人工合并。

脚本只复制项目规则文件，并创建空的本地 `USER.md`、`MEMORY.md`。这些本地文件用于用户资料与记忆，不应回写到项目目录。

### 4. 添加或核对 Agent

OpenClaw 的非默认 Agent 各自有 workspace、状态目录和会话存储。先查看当前状态：

```bash
openclaw agents list
```

若以下 Agent 不存在，再逐个添加。已有同 ID 的 Agent 不要重复添加；应先比较其 workspace 和安全配置。

```bash
export SESAME_OPENCLAW_HOME="${OPENCLAW_HOME:-$HOME/.openclaw}"

openclaw agents add sesame \
  --workspace "$SESAME_OPENCLAW_HOME/workspace-sesame" --non-interactive
openclaw agents add sesame-learning \
  --workspace "$SESAME_OPENCLAW_HOME/workspace-sesame-learning" --non-interactive
openclaw agents add sesame-children \
  --workspace "$SESAME_OPENCLAW_HOME/workspace-sesame-children" --non-interactive
openclaw agents add sesame-work \
  --workspace "$SESAME_OPENCLAW_HOME/workspace-sesame-work" --non-interactive
```

添加后，从各自 `IDENTITY.md` 同步身份：

```bash
openclaw agents set-identity --agent sesame --from-identity
openclaw agents set-identity --agent sesame-learning --from-identity
openclaw agents set-identity --agent sesame-children --from-identity
openclaw agents set-identity --agent sesame-work --from-identity
```

然后将 `config/agents.template.json` 视为目标状态，把每个同 ID 的配置**合并**到本机 `~/.openclaw/openclaw.json`。不要把模板作为 `openclaw config patch` 的输入：其中的 `agents.list` 是数组，直接 patch 会替换其他 Agent。

### 5. 注册 `sesame-scene` MCP bridge

根据 `config/sesame-scene-mcp.template.json`，在本机 OpenClaw 配置的 `mcp.servers.sesame-scene` 创建或更新条目：

- 将 `<V3_PROJECT_ROOT>` 替换为新电脑上的绝对项目路径；
- 使用 Gateway 虚拟环境内的 Python；
- 只允许三个场景工具；
- 将 `SESAME_SCENE_CONTROL_TOKEN` 设置为本地 SecretRef（密钥引用）或受控服务环境变量，不能把真实 token 留在版本化配置里；
- `SESAME_SCENE_GATEWAY_URL` 默认是 `http://127.0.0.1:8788`，Gateway 改端口时必须同步更新；
- `SESAME_DEFAULT_DEVICE_ID` 必须与 Gateway 的 `SESAME_DEVICE_ID` 相同。

当前 CLI 支持把配置值关联到环境变量。先用 `--dry-run` 验证，再执行实际写入：

```bash
openclaw config set \
  mcp.servers.sesame-scene.env.SESAME_SCENE_CONTROL_TOKEN \
  --ref-provider default --ref-source env \
  --ref-id SESAME_SCENE_CONTROL_TOKEN --dry-run
```

SecretRef 的具体 provider 设置与 OpenClaw 版本有关。若此命令在新版本报错，不要退回到提交明文 token；查 `openclaw config set --help` 和当前版本的配置 schema，或改用权限受控的本地服务环境变量。

### 6. 验证

```bash
openclaw config validate
openclaw agents list
openclaw mcp doctor
openclaw mcp probe sesame-scene
```

验证通过后，`sesame-scene` 应只列出 `list_scenes`、`get_scene`、`select_scene`。如果 probe 失败，按此顺序排查：Gateway 是否正在运行、MCP 配置中的 Python 与源码路径是否正确、两个进程的场景控制 token 是否一致、设备 ID 是否一致。

## 四个 Agent 的职责

| Agent ID | 模式 | 未来知识库命名空间 | 输出重点 |
| --- | --- | --- | --- |
| `sesame` | 正常 | `kb-normal` | 温和、清楚、简短。 |
| `sesame-learning` | 学习 | `kb-learning` | 结论、原因、一个下一步或小问题。 |
| `sesame-children` | 儿童 | `kb-children` | 亲切短句、故事问答、一次一个任务。 |
| `sesame-work` | 工作 | `kb-work` | 结论优先、待办、风险、下一步。 |

当前知识库命名空间只是设计约定，检索尚未接入。四个 Agent 的 `actions` 固定输出为空数组；它们只能选择 `default`、`happy`、`thinking` 表情。这个限制是有意的：在 `RobotDriver` 完成真实硬件验证前，不让 LLM 触发机器人动作。

## 本目录不能完成的事情

OpenClaw workspace 和 MCP bridge 配置完成后，仍然不代表整机语音链路已完成。以下项目仍需单独实现或由操作者做决定：

- ASR、TTS、Agent Adapter 的实际 Provider、模型、鉴权和隐私策略；
- OpenClaw 收到转写后如何由 Agent Adapter 发送 `agent-response.v1` 到 Gateway；
- 真实设备 WSS、TLS、私有 CA 和公网认证；
- `RobotAdapter` 到真实 `RobotDriver` 的硬件动作实现与安全验收；
- 知识库检索和每个模式的数据保留策略。

相关设计见 [ESP32 Opus + OpenClaw 技术设计](../../docs/technical-design/esp32-opus-openclaw-platform.md) 和 [Voice Gateway 后端实现指南](../../docs/implementation/voice-gateway-backend.md)。
