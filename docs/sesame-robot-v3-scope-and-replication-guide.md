# Sesame Robot V3：项目范围与跨设备快速复现指南

> 盘点日期：2026-08-12。本文面向需要在另一台电脑继续开发，或从零搭建当前 V3 方案的人。

## 先说结论

GitHub 上的 [dorianborian/sesame-robot](https://github.com/dorianborian/sesame-robot) 只能复现原版 Sesame 机器人，不能复现当前工作区的 V3 语音机器人方案。要在另一台设备上继续本项目，至少需要同时拿到 V3 固件、Endpoint Gateway、OpenClaw 项目模板和项目文档；仅执行 `git clone` 原版仓库是不够的。

当前工作区根目录还没有 Git 提交，也没有 GitHub 远端。因此下面列出的 V3 代码和文档目前并没有被任何远端仓库保存，应先整理为一个私有的交付仓库或离线源码包。

## 1. 三套内容的边界

| 内容集 | 位置或来源 | 作用 | 能否只靠原版 GitHub 得到 |
| --- | --- | --- | --- |
| 原版 Sesame | `github_refs/sesame-robot/`，远端为 `dorianborian/sesame-robot` | 3D 打印结构、PCB、装配和接线指南、Arduino 固件、Sesame Studio | 可以 |
| 当前 V3 方案 | `firmware-work/Sesame_Robot_V3_IDF/`、`endpoint-gateway/`、`ops/openclaw/`、`docs/` | ESP32-S3 语音固件、WSS 协议、电脑 Gateway、OpenClaw workspace/MCP 模板和系统设计 | 不可以 |
| 本机过程材料 | `output/`、`restore-archives/`、`github_refs/`、`.internal/` 等 | 历史补丁、备份、第三方参考仓库和本机运行状态 | 不应作为项目源码交付 |

原版仓库实际包含以下内容：

- `hardware/`：BOM、STL、CAD、PCB 资料；
- `docs/`：打印、装配、接线说明和表情图片；
- `firmware/`：Arduino 基础固件、动作序列、屏幕表情和电机测试；
- `software/sesame-studio/`：Python 动作编排工具。

README 提到的 Sesame Simulator 和 Sesame Companion App 是外部项目链接，不是原版仓库内的代码。

## 2. 原版 GitHub 不包含什么

下面这些是当前 V3 工作的关键部分，原版仓库没有：

| 缺失部分 | 当前本地位置 | 说明 |
| --- | --- | --- |
| ESP-IDF 正式固件 | `firmware-work/Sesame_Robot_V3_IDF/` | 固定为 ESP32-S3、4 MB Flash；包含 I2S 音频、Opus、WakeWord、WSS、NVS 配置和协议测试。 |
| 电脑端 Endpoint Gateway | `endpoint-gateway/` | FastAPI 网关、设备 WebSocket、开发控制台、场景切换和 OpenClaw MCP bridge。 |
| 本地 OpenClaw 项目模板 | `ops/openclaw/` | 四个 Sesame Agent 的脱敏 workspace、受限场景 MCP bridge、安装/验证说明；不含本机凭据与会话。 |
| V3 架构和实施资料 | `docs/architecture/`、`docs/implementation/`、`docs/technical-design/` | UART/Wi-Fi/WSS 设计、Voice Gateway、OpenClaw、ASR/TTS、隐私和每日小日记方案。 |
| 音频实验与硬件诊断 | `firmware/INMP441_Audio_Test/`、`flash-diagnostics/` | INMP441 音频验证和刷机问题排查。 |
| 动作创作与辅助工具 | `Bottango/`、`tools/`、`assets/` | 动作编排和开发辅助材料；是否交付取决于是否继续维护动作资产。 |

即使拿到了上述源码，下面三项仍然不是“已有代码直接带来的能力”：

1. `ops/openclaw/` 已提供本地 Agent 和 MCP 配置模板，但 `endpoint-gateway` 没有可直接使用的 ASR、TTS 和 Agent Adapter Provider 配置。服务地址、鉴权、模型和隐私策略仍须按实际部署补齐。
2. V3 固件中的 `RobotAdapter` 尚未接入真实 `RobotDriver`。Gateway 可以验证、转发控制消息并显示回执，但代码目前不能保证真实舵机动作已经执行。
3. 真实设备连接要求 `wss://.../v1/device-stream`、设备 token、私有根证书和每台设备的 NVS 配置。开发控制台的 `http://127.0.0.1:8788/` 不能代替设备侧的 TLS 部署。

因此，“另一台电脑能启动测试”和“整机能完成真实语音、动作和生产部署”是两个不同的验收层级。

## 3. 新设备应带走什么，不应带走什么

建议建立一个新的私有仓库，例如 `sesame-robot-v3`。它的目标是保存可重建的源码和文档，而不是复制整个工作区。

### 必须纳入交付仓库

```text
sesame-robot-v3/
├── endpoint-gateway/
│   ├── pyproject.toml
│   ├── README.md
│   ├── src/
│   └── tests/
├── firmware-work/
│   └── Sesame_Robot_V3_IDF/
│       ├── main/ components/ artifacts/ tools/ tests/
│       ├── provisioning/device-config.example.json
│       ├── CMakeLists.txt  sdkconfig.defaults  sdkconfig
│       └── dependencies.lock
├── firmware/INMP441_Audio_Test/       # 如继续调试麦克风
├── ops/openclaw/                       # OpenClaw workspace/MCP 的脱敏模板
├── docs/
├── Bottango/                          # 如继续制作动作
├── tools/ assets/ flash-diagnostics/  # 按实际开发需要保留
└── UPSTREAM.md                        # 记录原版 Sesame 的 URL 和固定 commit
```

`artifacts/` 中的唤醒词模型是 V3 固件的一部分，不能因为它是二进制文件就删掉。`dependencies.lock` 和 `sdkconfig.defaults` 也必须保留，否则另一台电脑会失去依赖版本和构建基线。

如果新设备还要打印、装配或维修机体，必须另外克隆原版 Sesame 的固定 commit，取得其中的 `hardware/` 和原版装配文档。V3 工作区没有一份独立、可替代的机械结构资料。为避免把上游源码混入自己的历史，建议在新设备单独放到 `upstream/sesame-robot/`。

### 不应纳入交付仓库

| 排除内容 | 原因 |
| --- | --- |
| `endpoint-gateway/.venv/` | 本机 Python 虚拟环境，当前约 27 MB，可由 `pyproject.toml` 重建。 |
| `firmware-work/Sesame_Robot_V3_IDF/.venv-wakeword/` | 唤醒词训练环境，当前约 1.1 GB；除非需要训练模型，否则不应复制。 |
| `firmware-work/Sesame_Robot_V3_IDF/build/`、`build-host-tests/`、`firmware/INMP441_Audio_Test/build/` | 构建产物，可重新生成；当前分别约 362 MB、0.4 MB 和 72 MB。 |
| `firmware-work/Sesame_Robot_V3_IDF/managed_components/` | 可由 ESP-IDF 和 `dependencies.lock` 下载重建，约 513 MB；只有离线部署时才随离线包携带。 |
| `output/`、`restore-archives/`、`git-diff-demo/`、`.internal/`、`.playwright-cli/`、`.DS_Store` | 历史任务输出、备份、演示或本机状态，不是运行所需源码。 |
| `github_refs/` | 第三方参考仓库的本地副本；保留来源 URL、commit 和许可证即可，不要把整份副本混进项目代码。 |
| `~/.openclaw/openclaw.json`、`credentials/`、`agents/*/sessions/`、`sandboxes/` | 本机 OpenClaw 的配置、模型凭据、会话和运行状态。只迁移 `ops/openclaw/` 中的模板。 |
| `.env`、真实 `device-config.json`、`device-nvs.bin`、token、Wi-Fi 密码、私有 CA 证书 | 这些是凭据或敏感配置。每台设备单独生成和保存，绝不提交。 |

一个交付仓库至少应有如下 `.gitignore` 规则：

```gitignore
.DS_Store
**/.venv/
**/.venv-*/
**/__pycache__/
**/build/
**/build-host-tests/
**/managed_components/
**/.env
**/device-config.json
**/device-nvs.bin
output/
restore-archives/
github_refs/
.internal/
.playwright-cli/
**/.openclaw/
```

如果需要在完全离线的新设备上构建 ESP-IDF 固件，可以把 `managed_components/` 放入一次性的加密离线包；它仍不应成为日常 Git 历史的一部分。

Gateway 的 `pyproject.toml` 当前只声明 FastAPI、Uvicorn 和 `httpx` 的版本范围，没有 Python 依赖锁文件。因此首次在新设备验证通过后，应把实际安装版本导出为 `requirements.lock.txt` 并提交；在此之前，它是“可重建的开发环境”，不是字节级完全一致的环境。

## 4. 两种快速复现方式

### 方式 A：私有 Git 仓库，适合长期开发

这是推荐方式。先从本机复制“必须纳入交付仓库”的内容，建立私有 GitHub 仓库，确认没有凭据后提交。新设备只需要克隆私有仓库，然后按第 5 节安装运行环境。

优点是后续改动可追踪、多人或多设备能同步；代价是新设备首次构建时需要下载 Python 和 ESP-IDF 依赖。

原版 Sesame 应保留为一个明确的上游依赖，而不是和 V3 改动混在同一个目录。`UPSTREAM.md` 至少记录：

```text
upstream: https://github.com/dorianborian/sesame-robot
purpose: 原版机械结构、硬件资料和 Arduino 基线
pin: <实际使用的 commit SHA>
license: Apache-2.0
```

### 方式 B：离线源码包，适合立刻迁移到另一台电脑

当两台电脑在同一地点，或新设备不方便联网时，创建一个仅含“必须纳入交付仓库”的压缩包并通过 U 盘或安全局域网传输。默认不带虚拟环境和构建目录；如果目标设备需要完全离线构建，再额外携带 `managed_components/`。

不要直接压缩当前工作区根目录。它包含约 3.1 GB 的 `firmware-work/`、`output/`、备份和第三方参考副本，其中大部分不能帮助新设备运行代码，反而会把历史材料与源码边界混在一起。

离线包传输完成后，应计算 SHA-256 校验值并在新设备重新核对。压缩包只能解决“源码和依赖搬运”，不能替代每台设备独有的 Wi-Fi、token、CA 证书和 NVS 生成流程。

## 5. 新设备的最小启动顺序

先跑没有硬件、没有真实语音 Provider 的测试路径，再逐步接设备。这样可以先分清是环境问题、网关问题还是硬件问题。

### 5.1 Endpoint Gateway：本机开发控制台

前置条件：Python 3.9 或更高版本。项目声明的运行依赖是 FastAPI 和 Uvicorn，测试额外使用 `httpx`。

```bash
cd /path/to/sesame-robot-v3/endpoint-gateway
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e ".[test]"
python -m unittest discover -s tests -v

export SESAME_DEVICE_ID='sesame-v3-001'
export SESAME_DEVICE_TOKEN='replace-with-a-new-device-token'
export SESAME_GATEWAY_ID='sesame-edge'
python -m uvicorn sesame_endpoint_gateway.server:create_runtime_app --factory --host 127.0.0.1 --port 8788
```

浏览器打开 `http://127.0.0.1:8788/`。这一阶段能验证网关、设备协议和开发控制台；它不等于已经接入真实 ASR、TTS、OpenClaw 或真实舵机。

如需使用场景控制，还要单独在 Gateway 和 OpenClaw MCP bridge 中配置同一个本地控制 token：

```bash
export SESAME_SCENE_CONTROL_TOKEN='generate-a-local-secret'
export SESAME_SCENE_GATEWAY_URL='http://127.0.0.1:8788'
```

### 5.2 V3 ESP-IDF 固件：先跑主机测试，再构建

前置条件：安装与当前工程一致的 ESP-IDF v5.5.4，并使用 ESP32-S3 工具链。不要复制旧电脑的 `/Users/mac/...` 路径；在新设备上找到 ESP-IDF 的 `export.sh` 后再 source。

```bash
cd /path/to/sesame-robot-v3/firmware-work/Sesame_Robot_V3_IDF
source /path/to/esp-idf-v5.5.4/export.sh
bash tests/run_host_tests.sh
idf.py set-target esp32s3
idf.py build
```

固件固定目标为 ESP32-S3、4 MB Flash，音频硬件基线是 INMP441 麦克风和 MAX98357A 功放。主机测试通过后才连接实机。烧录时使用项目脚本：

```bash
bash tools/flash.sh
```

如果同时连接了多个串口设备，明确指定端口，例如：

```bash
bash tools/flash.sh /dev/cu.usbmodem101
```

### 5.3 每台实机的私密配置

复制 `provisioning/device-config.example.json` 到仓库之外的受保护目录，填写该设备自己的 Wi-Fi、设备 ID、Gateway URL、token 和根证书路径，再生成 NVS 镜像。不要把生成后的 JSON 或 `device-nvs.bin` 放回 Git。

```bash
python3 tools/generate_nvs.py /private/path/device-config.json build/device-nvs.bin
esptool.py --chip esp32s3 --port /dev/cu.usbmodem101 write_flash \
  0x9000 build/device-nvs.bin
```

真实设备的 `gateway_url` 必须是 `wss://` 地址，并以 `/v1/device-stream` 结束。开发控制台只监听本机 HTTP；设备 WSS 需要额外的 TLS 终止层、私有 CA 和网络部署。

## 6. 复现验收清单

在新设备上按顺序确认，任何一步失败都不要跳到后面的整机问题排查：

1. Gateway 单元测试全部通过。
2. Gateway 本机控制台能打开，未知动作或表情会被拒绝。
3. ESP-IDF 主机测试通过，`idf.py build` 成功。
4. 每台设备的 NVS 已单独生成，真实凭据不在仓库和终端日志中。
5. 设备通过 TLS 成功连到 `/v1/device-stream`。
6. ASR、TTS、OpenClaw Provider 已按实际服务配置并做隐私审查。
7. `RobotDriver` 已接入，并在断电、急停和受限动作条件下验证真实舵机执行。

在本机盘点时已执行两组现有测试：Endpoint Gateway 的 14 项 Python 单元测试全部通过；`firmware-work/Sesame_Robot_V3_IDF/tests/run_host_tests.sh` 以成功退出状态完成，并验证了唤醒词资产与 Wi-Fi 策略。它们证明当前代码的网关和主机侧协议基线可测，不证明外部 Provider、TLS 部署或真实机器人动作已经完成。

## 7. 推荐的下一步

先把第 3 节的“必须纳入交付仓库”整理到一个私有 `sesame-robot-v3` 仓库，并做一次不含凭据的首次提交。之后用第 5 节在另一台电脑验证 Gateway 测试和 ESP-IDF 构建。只有这两步稳定后，再接入 TLS、ASR/TTS/OpenClaw Provider 和真实 `RobotDriver`。这样能避免把原版 Sesame、V3 源码、备份和设备密钥混成一个无法复现的目录。
