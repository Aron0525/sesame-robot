# Sesame Robot V3：交给 AI 的新电脑配置任务书

> 使用方式：在新电脑上，把本文、原版 Sesame 仓库，以及 V3 补充源码包或私有仓库一起交给 AI。本文是配置任务说明，不是 V3 源码本身。

## 1. 任务目标和边界

目标是在新电脑上建立当前 Sesame Robot V3 的**可开发基线**：Endpoint Gateway 测试通过、Gateway 可以在本机启动、本地 OpenClaw 的四个 Sesame Agent 与 MCP bridge 配置完成、ESP-IDF 固件通过主机测试并成功构建。随后，只有在用户明确提供设备和部署信息时，才继续烧录、WSS、ASR、TTS 和真实动作。

不要把原版 GitHub 仓库当成完整项目。`https://github.com/dorianborian/sesame-robot` 只包含原版 Sesame 的机械结构、硬件资料、Arduino 固件和 Sesame Studio；它不包含本任务所需的 V3 固件、电脑 Gateway、V3 架构文档和本机诊断材料。

若 AI 只有原版 GitHub 仓库和本文，没有下面列出的 V3 源码，必须明确报告“缺少源码，无法复现 V3 实现”，并要求用户提供私有 V3 仓库或离线源码包。不得根据架构文档臆造、重写或假装找回缺失代码。

## 2. 必须取得的输入

### 2.1 原版上游仓库

```text
https://github.com/dorianborian/sesame-robot
```

用途是获取 3D 打印、PCB、BOM、装配/接线资料，以及原版 Arduino 基线。只有需要制作、装配或维修机体时才需要它；它不是 V3 Gateway 或 V3 ESP-IDF 固件的来源。

### 2.2 V3 源码包或私有仓库

下面的路径必须真实存在。它们是当前电脑上有、原版 GitHub 没有的内容。

```text
endpoint-gateway/
  pyproject.toml
  README.md
  src/sesame_endpoint_gateway/
  tests/

firmware-work/Sesame_Robot_V3_IDF/
  main/
  components/
  artifacts/
  provisioning/device-config.example.json
  tools/
  tests/
  CMakeLists.txt
  sdkconfig.defaults
  sdkconfig
  dependencies.lock

docs/architecture/
docs/implementation/
docs/technical-design/

ops/openclaw/
  README.md
  config/
  workspaces/
  scripts/install_workspaces.sh
```

按需要同时带上：

```text
firmware/INMP441_Audio_Test/   # 调试 INMP441 麦克风
flash-diagnostics/            # 刷机诊断
Bottango/ tools/ assets/       # 动作资产和辅助工具
```

`firmware-work/Sesame_Robot_V3_IDF/artifacts/` 中的唤醒词模型是构建输入，不能删除。`dependencies.lock`、`sdkconfig.defaults` 和 `sdkconfig` 是依赖版本与 ESP32-S3 构建基线，必须保留。

### 2.3 只有进入真实设备或生产部署才需要的用户输入

AI 不得猜测、生成或把这些值写入源码。缺少它们时，停在本机开发验证阶段即可。

| 进入阶段 | 用户必须提供 |
| --- | --- |
| 设备烧录 | ESP32-S3 串口、Wi-Fi SSID/密码、唯一设备 ID、唯一设备 token |
| 设备连网 | `wss://` Gateway 地址、私有根 CA 证书或证书部署方式 |
| 语音与 Agent | ASR Provider、TTS Provider、模型鉴权、Agent Adapter、数据保留和隐私决定；OpenClaw 本地配置按 `ops/openclaw/` 模板恢复。 |
| 真实舵机动作 | 已实现并验收的 `RobotDriver`、硬件接线和安全测试条件 |
| 公网部署 | TLS 终止方案、操作员认证、设备选择与审计存储方案 |

## 3. 不要迁移或提交的内容

以下内容不是可重建源码。不要复制到日常 Git 仓库，不要把它们当作“项目已完整备份”的证据。

```text
endpoint-gateway/.venv/
firmware-work/Sesame_Robot_V3_IDF/.venv-wakeword/
firmware-work/Sesame_Robot_V3_IDF/build/
firmware-work/Sesame_Robot_V3_IDF/build-host-tests/
firmware-work/Sesame_Robot_V3_IDF/managed_components/  # 仅离线构建包需要
firmware/INMP441_Audio_Test/build/
output/
restore-archives/
github_refs/
git-diff-demo/
.internal/
.playwright-cli/
.DS_Store
~/.openclaw/openclaw.json
~/.openclaw/credentials/
~/.openclaw/agents/*/sessions/
~/.openclaw/sandboxes/
```

永远排除 `.env`、真实 `device-config.json`、`device-nvs.bin`、token、Wi-Fi 密码和私有 CA 证书。对于完全离线的 ESP-IDF 构建，可以把 `managed_components/` 放进一次性的受控离线包，但不要纳入常规 Git 历史。

## 4. 推荐目录与初始检查

建议将自己的 V3 代码与上游原版分开放置，避免覆盖原版 Arduino 固件或把上游文件误提交为自己的改动。

```text
<workspace>/
├── sesame-robot-v3/             # 私有 V3 源码包或私有仓库
│   ├── endpoint-gateway/
│   ├── firmware-work/Sesame_Robot_V3_IDF/
│   ├── docs/
│   └── ops/openclaw/
├── upstream/
│   └── sesame-robot/            # 原版 GitHub 的固定 commit
└── private-device-config/       # Git 之外，权限受控
```

开始前执行只读检查：

```bash
cd <workspace>/sesame-robot-v3
test -f endpoint-gateway/pyproject.toml
test -d endpoint-gateway/src/sesame_endpoint_gateway
test -d endpoint-gateway/tests
test -f firmware-work/Sesame_Robot_V3_IDF/CMakeLists.txt
test -f firmware-work/Sesame_Robot_V3_IDF/dependencies.lock
test -d firmware-work/Sesame_Robot_V3_IDF/artifacts
test -f firmware-work/Sesame_Robot_V3_IDF/provisioning/device-config.example.json
test -f ops/openclaw/README.md
test -f ops/openclaw/config/agents.template.json
test -f ops/openclaw/config/sesame-scene-mcp.template.json
```

其中任何一项不存在时，不进入构建。列出缺失路径并请求补充，不要以 `output/`、备份目录或第三方参考仓库代替。

## 5. 第一阶段：配置并验证 Endpoint Gateway

Gateway 是电脑端的 FastAPI 服务：它接收 ESP32-S3 的设备 WebSocket，提供本机开发控制台，并提供场景选择的 OpenClaw MCP bridge。它不在 ESP32 上运行 ASR、TTS 或 Agent。

前置条件：Python 3.9 或更高版本。项目声明依赖 FastAPI、Uvicorn；测试额外需要 `httpx`。

```bash
cd <workspace>/sesame-robot-v3/endpoint-gateway
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e ".[test]"
PYTHONPATH=src python -m unittest discover -s tests -v
```

若可编辑安装因当前项目的 Python 打包元数据失败，保留失败输出，不要用复制源码或全局安装掩盖问题。可以先在虚拟环境中安装 `pyproject.toml` 声明的依赖，并在所有本地命令中显式使用 `--app-dir src` 或 `PYTHONPATH=src`。如用户授权修改项目配置，再补齐可编辑安装所需的打包配置并重新运行测试。

Gateway 当前的源代码位于 `src/`。未安装为包时，下面这条**稳定的本地启动命令**必须带 `--app-dir src`：

```bash
export SESAME_DEVICE_ID='sesame-v3-001'
export SESAME_DEVICE_TOKEN='replace-with-a-new-device-token'
export SESAME_GATEWAY_ID='sesame-edge'

python -m uvicorn --app-dir src \
  sesame_endpoint_gateway.server:create_runtime_app \
  --factory --host 127.0.0.1 --port 8788
```

在浏览器访问 `http://127.0.0.1:8788/`。若端口 `8788` 被占用，选一个未占用的本机端口；不要停止未知进程。控制台只能验证开发链路，不能代替设备 TLS、真实语音服务或物理动作。

如用户要求验证场景选择，再设置同一个本地控制 token：

```bash
export SESAME_SCENE_CONTROL_TOKEN='generate-a-local-secret'
export SESAME_SCENE_GATEWAY_URL='http://127.0.0.1:8788'
```

当前场景为 `normal`、`learning`、`children` 和 `work`。OpenClaw MCP bridge 只应暴露读取/选择场景的能力，不能取得舵机控制或通用系统权限。

## 6. 第二阶段：配置本地 OpenClaw

OpenClaw 是本机运行时，但其 Sesame workspace 模板和 MCP bridge 配置属于项目内容。先完整阅读 [`ops/openclaw/README.md`](../../ops/openclaw/README.md)，再按其顺序操作：安装并验证指定 OpenClaw 版本、安装四个 workspace 模板、添加或核对四个 Agent、合并受限 Agent 配置、注册并 probe `sesame-scene` MCP server。

AI 必须遵守以下边界：

1. 不能复制旧电脑整个 `~/.openclaw/`，其中包含凭据、身份、会话、sandbox 和个人记忆。
2. `ops/openclaw/config/agents.template.json` 是合并参考，不能直接 patch，因为它的 `agents.list` 会替换现有数组。
3. `SESAME_SCENE_CONTROL_TOKEN` 必须同时存在于 Gateway 和 MCP bridge 的本地 SecretRef（密钥引用）或受控服务环境变量中，绝不提交明文。
4. 通过 `openclaw config validate`、`openclaw agents list`、`openclaw mcp doctor` 和 `openclaw mcp probe sesame-scene` 后，才能报告 OpenClaw 基线完成。
5. `sesame-scene` 只允许 `list_scenes`、`get_scene`、`select_scene`；不得增加动作、shell 或设备控制工具。

OpenClaw 配置成功仍不等于语音 Agent 已接通。ASR、TTS 和 Agent Adapter 尚需要用户提供实际 Provider、鉴权和隐私方案。

## 7. 第三阶段：配置并验证 ESP-IDF 固件

固件目标是 ESP32-S3、4 MB Flash，使用 ESP-IDF v5.5.4 和 ESP32-S3 工具链。不要复制旧电脑的绝对路径；在新电脑找到自己的 `esp-idf-v5.5.4/export.sh`。

```bash
cd <workspace>/sesame-robot-v3/firmware-work/Sesame_Robot_V3_IDF
source <esp-idf-v5.5.4 的绝对路径>/export.sh
bash tests/run_host_tests.sh
idf.py set-target esp32s3
idf.py build
```

先报告主机测试和构建结果。只有两者成功，并且用户确认连接的是目标板时，才继续烧录：

```bash
bash tools/flash.sh <ESP32-S3 串口>
```

脚本可以自动识别唯一串口，但 AI 不应在有多个串口时猜测。用户未提供端口时，要求其确认。ESP32-S3 进不了下载模式时，按住 BOOT（GPIO0），短按 RESET/EN，看到 `Connecting...` 后再松开 BOOT。

V3 音频硬件基线如下；接线与板型不一致时，不烧录后假设音频能工作。

| 信号 | GPIO |
| --- | ---: |
| I2S BCLK / INMP441 SCK | 14 |
| I2S WS / LRCLK | 47 |
| INMP441 SD | 48 |
| MAX98357A DIN | 2 |
| MAX98357A SD/EN | 1 |

INMP441 的 L/R 接地，使用 left slot。设备与 Gateway 固定协商 `Opus / 16 kHz / mono / 20 ms`；不要在没有同步修改协议和测试的情况下改变这些值。

## 8. 第四阶段：仅在用户提供配置后接入真实设备

每台设备都要从模板生成自己的 NVS 配置。把真实 JSON 放在 Git 工作树外的受保护位置。

```bash
cd <workspace>/sesame-robot-v3/firmware-work/Sesame_Robot_V3_IDF
source <esp-idf-v5.5.4 的绝对路径>/export.sh
python3 tools/generate_nvs.py \
  <受保护目录>/device-config.json \
  build/device-nvs.bin
esptool.py --chip esp32s3 --port <已确认的串口> \
  write_flash 0x9000 build/device-nvs.bin
```

`device-config.json` 需要 Wi-Fi、设备 ID、Gateway ID、`wss://.../v1/device-stream`、设备 token 和根证书路径。NVS 与固件分开烧录。生成的 `device-nvs.bin` 包含敏感信息，权限应设为仅授权用户可读写，并按设备资产流程保管或销毁。

本机 `http://127.0.0.1` 控制台不是设备 WSS 服务。真实设备必须经过 TLS 终止层，使用私有 CA 和 `wss://.../v1/device-stream`。未获得用户选择的部署位置、证书和认证方案时，不要把 Gateway 暴露到公网。

## 9. 当前不能自动完成的范围

以下事情在现有代码中仍未落地或未配置。AI 必须明确标为“待用户决定/待实现”，不能说已完成。

1. ASR、TTS 和 Agent Adapter：OpenClaw 的本地 workspace 与 MCP 模板已提供，但没有实际 Provider URL、API Key、模型、鉴权或隐私策略配置。
2. 公网 TLS、操作员认证、设备选择和审计存储：开发控制台只适合单机开发。
3. 真实舵机执行：`RobotAdapter` 尚未接入实际 `RobotDriver`；Gateway 回执、页面状态和协议通过，不代表机器人已移动。
4. 知识库检索：四个模式有命名空间设计，但当前未接入检索实现。
5. 生产级安全：Flash Encryption（Flash 加密）与 NVS Encryption（NVS 加密）尚需按量产方案启用。

不得为“让它看起来可用”而偷偷接入云端语音服务、把 token 放进网页、放宽动作白名单、绕过 WSS 证书验证，或把控制台产生的状态当成设备真实遥测。

## 10. 完成定义与交接输出

AI 只能根据实际证据报告完成。阶段一和二完成时，输出：

```text
已取得的源码：<路径或仓库 commit>
Gateway：<依赖安装结果；单元测试结果；实际启动命令与端口>
OpenClaw：<版本；四个 Agent；config validate、mcp doctor/probe 结果>
固件：<ESP-IDF 版本；主机测试结果；idf.py build 结果>
未执行：<烧录/WSS/ASR/TTS/OpenClaw/RobotDriver 中尚未授权或缺少输入的项目>
敏感信息：未写入 Git、文档、终端回显或网页
阻塞项：<若有，写出缺少的确切文件、配置或用户决定>
```

只有在用户提供设备信息并明确授权后，才把“固件构建成功”推进到“设备烧录成功”；只有观察到设备真实回执和受控物理测试，才把“Gateway 可启动”推进到“机器人物理动作已验证”。
