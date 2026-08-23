# Sesame Robot V3：换电脑后的搭建指南

> 本文给准备在另一台电脑继续开发的人使用。它先帮助你把开发环境跑起来，再决定是否连接真实机器人。

## 先确认：GitHub 原版并不等于当前项目

原版仓库是 [dorianborian/sesame-robot](https://github.com/dorianborian/sesame-robot)。它有机器人外壳、3D 打印文件、PCB、装配资料、Arduino 固件和 Sesame Studio，适合做一台原版 Sesame。

当前电脑上的 V3 项目还多了 ESP32-S3 语音固件、电脑端 Gateway、WSS 通信、OpenClaw 场景接口和相关设计文档。这些内容不在原版 GitHub 中。只克隆原版仓库，不能在新电脑上继续当前 V3 方案。

本文也不能替代源码。开始前，你必须同时拿到原版仓库和一份 V3 源码包或私有 V3 仓库；如果 V3 源码没有被交给你，先索取它，不要试图从文档把缺失代码重新拼出来。

## 1. 需要准备的东西

### 代码

新电脑建议建立以下目录：

```text
<工作目录>/
├── sesame-robot-v3/             # 你的 V3 源码
│   ├── endpoint-gateway/
│   ├── firmware-work/Sesame_Robot_V3_IDF/
│   ├── docs/
│   └── ops/openclaw/
└── upstream/
    └── sesame-robot/            # 原版 Sesame 仓库
```

V3 源码中至少要有：

- `endpoint-gateway/`：电脑上的 Gateway 和开发控制台；
- `firmware-work/Sesame_Robot_V3_IDF/`：ESP32-S3 的正式固件；
- `docs/architecture/`、`docs/implementation/`、`docs/technical-design/`：当前方案的设计和实施说明；
- `ops/openclaw/`：四个本地 OpenClaw Agent 的脱敏 workspace 模板、MCP bridge 模板和安装说明；
- `artifacts/`、`dependencies.lock`、`sdkconfig.defaults`：唤醒词模型、依赖和构建基线，不能删。

如果准备制作或维修机器人，再克隆原版仓库，用其中的 `hardware/` 和装配文档。原版仓库不应覆盖 V3 目录。

### 软件和硬件

| 用途 | 要准备的内容 |
| --- | --- |
| Gateway | Python 3.9 或更高版本、浏览器 |
| OpenClaw | 本机 OpenClaw；当前已验证版本为 `2026.7.1-1` |
| V3 固件 | ESP-IDF v5.5.4、ESP32-S3 工具链、C++ 编译器 |
| 实机烧录 | ESP32-S3 4 MB Flash 开发板、支持数据传输的 USB 线 |
| 音频硬件 | INMP441 麦克风、MAX98357A 功放和扬声器 |
| 实机联网 | Wi-Fi、每台设备独有的 token、私有根证书和 WSS 地址 |

## 2. 不要直接复制的文件

换电脑时不要把整个旧工作区打包。虚拟环境、构建结果、历史输出和备份会占用大量空间，而且不能代替源码。

不要复制或提交：

- `endpoint-gateway/.venv/`、`.venv-wakeword/`；
- 所有 `build/`、`build-host-tests/`；
- `output/`、`restore-archives/`、`github_refs/`、`.internal/`；
- `.env`、真实 `device-config.json`、`device-nvs.bin`、token、Wi-Fi 密码和私有 CA 证书。
- 整个 `~/.openclaw/`：其中有本机模型凭据、身份、会话、sandbox 和个人记忆，不应整体复制或提交。

其中 `managed_components/` 默认也不需要带走，因为 ESP-IDF 能按照 `dependencies.lock` 重新下载。只有新电脑完全不能联网构建时，才把它作为单独的受控离线包带过去。

## 3. 先启动电脑端 Gateway

先验证 Gateway，不连接机器人也可以完成这一步。

```bash
cd <工作目录>/sesame-robot-v3/endpoint-gateway
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e ".[test]"
PYTHONPATH=src python -m unittest discover -s tests -v
```

测试通过后，设置只用于本机开发的设备身份和 token，再启动服务：

```bash
export SESAME_DEVICE_ID='sesame-v3-001'
export SESAME_DEVICE_TOKEN='replace-with-a-new-device-token'
export SESAME_GATEWAY_ID='sesame-edge'

python -m uvicorn --app-dir src \
  sesame_endpoint_gateway.server:create_runtime_app \
  --factory --host 127.0.0.1 --port 8788
```

然后在浏览器打开 `http://127.0.0.1:8788/`。页面能打开，且 Gateway 测试通过，说明电脑端开发基线已经建立。

这里有一个容易遇到的问题：当前 Gateway 源码放在 `src/` 下。如果启动时看到 `No module named 'sesame_endpoint_gateway'`，请确认命令包含 `--app-dir src`，不要把 `src` 目录复制到系统 Python 中。若端口 `8788` 已被使用，换一个没有被占用的端口，不要强行停止不认识的程序。

Gateway 当前是单台机器人、本机开发控制台。它能验证设备协议、动作白名单、表情白名单和场景选择；它不是公网服务，也不等于机器人已经会说话或会动。

## 4. 配置本地 OpenClaw

OpenClaw 应运行在这台电脑上，不运行在 ESP32。V3 项目现在已经有一份可迁移的 OpenClaw 配置板块：[ops/openclaw/README.md](../../ops/openclaw/README.md)。它保存四个 Sesame Agent 的规则、受限 MCP bridge 模板和安装步骤，但不包含你的聊天记录、模型密钥或个人记忆。

按该文档完成以下事情：

1. 安装并确认本机 OpenClaw 版本；
2. 复制 `sesame`、`sesame-learning`、`sesame-children`、`sesame-work` 四个 workspace 模板；
3. 将 Agent 配置安全地合并到本机 `~/.openclaw/openclaw.json`，不覆盖其他 Agent；
4. 用同一个本地控制 token 将 `sesame-scene` MCP bridge 连接到已经启动的 Endpoint Gateway；
5. 执行 `openclaw config validate`、`openclaw mcp doctor`、`openclaw mcp probe sesame-scene`。

完成后，OpenClaw 能使用三个受限工具读取或切换机器人场景。它仍不能直接操控舵机，也不表示 ASR、TTS 或实际对话链路已经接通。

## 5. 构建 ESP32-S3 固件

固件固定使用 ESP32-S3、4 MB Flash 和 ESP-IDF v5.5.4。先找到新电脑上 ESP-IDF 的安装位置，不要复制旧电脑的绝对路径。

```bash
cd <工作目录>/sesame-robot-v3/firmware-work/Sesame_Robot_V3_IDF
source <新电脑上的 esp-idf-v5.5.4 路径>/export.sh
bash tests/run_host_tests.sh
idf.py set-target esp32s3
idf.py build
```

只有主机测试和 `idf.py build` 都成功后，再连接 ESP32-S3。烧录时最好明确指定串口：

```bash
bash tools/flash.sh <ESP32-S3 串口>
```

若固件工具找不到板子，先换一根能传输数据的 USB 线。如果 ESP32-S3 没有自动进入下载模式，按住 BOOT，短按 RESET/EN，看到 `Connecting...` 后再松开 BOOT。

音频接线使用以下固定引脚。接线不一致时，不要把音频问题当成软件问题。

| 模块信号 | ESP32-S3 GPIO |
| --- | ---: |
| INMP441 SCK / I2S BCLK | 14 |
| INMP441 WS / I2S WS | 47 |
| INMP441 SD | 48 |
| MAX98357A DIN | 2 |
| MAX98357A SD/EN | 1 |

INMP441 的 L/R 接地。音频链路的固定格式为 `Opus / 16 kHz / 单声道 / 20 ms`；不要只改设备或只改 Gateway 的一端。

## 6. 给每台设备写入自己的网络配置

不要把 Wi-Fi 和设备密钥写进代码。复制 `provisioning/device-config.example.json` 到仓库外的私密目录，填入该设备的 Wi-Fi、设备 ID、Gateway ID、设备 token、根证书路径和 WSS 地址，再生成 NVS 文件。

```bash
cd <工作目录>/sesame-robot-v3/firmware-work/Sesame_Robot_V3_IDF
source <新电脑上的 esp-idf-v5.5.4 路径>/export.sh
python3 tools/generate_nvs.py \
  <私密目录>/device-config.json \
  build/device-nvs.bin
esptool.py --chip esp32s3 --port <已确认的串口> \
  write_flash 0x9000 build/device-nvs.bin
```

真实设备的 Gateway 地址必须是以 `wss://` 开头、以 `/v1/device-stream` 结束的地址。`device-nvs.bin` 包含敏感信息，保留在受控位置，不提交到 Git，也不要发到聊天记录里。

浏览器里打开的 `http://127.0.0.1:8788/` 只适合本机开发。要让机器人通过网络连接，仍需要配置 TLS、私有 CA 和真正的 WSS 服务。

## 7. 目前还不能直接得到的能力

即使 Gateway 能打开、固件能构建，也不要认为项目已经全部完成。以下内容仍需要后续决定或开发：

- OpenClaw 的本地 Agent 和 MCP 配置已有模板，但 ASR、TTS、Agent Adapter 还没有现成的地址、模型、鉴权和隐私配置；
- 公网部署还没有操作员认证、设备选择和审计存储；
- `RobotAdapter` 还没有接入真实 `RobotDriver`，所以页面上的回执不等于舵机已经执行动作；
- 场景有 `normal`、`learning`、`children`、`work`，但知识库检索还没有接入；
- 量产需要额外启用 Flash Encryption（Flash 加密）和 NVS Encryption（NVS 加密）。

不要为求快而把设备 token 写进网页、关闭 WSS 证书验证，或随意接入未知云端语音服务。先确定服务地址、数据是否离开局域网、密钥怎样保管，再接入真实语音和 Agent。

## 8. 判断你已经搭建到哪一步

| 看到的结果 | 可以确认 | 还不能确认 |
| --- | --- | --- |
| Gateway 测试通过、控制台打开 | 本机 Gateway 和控制台能运行 | 真实设备能连接或能动作 |
| OpenClaw 配置和 MCP probe 通过 | 四个本地 Sesame Agent 和场景 MCP bridge 已就绪 | ASR/TTS/Agent Adapter 或真实对话已接通 |
| 主机测试通过、`idf.py build` 成功 | 固件代码和工具链能构建 | 烧录成功、音频正常、网络可用 |
| NVS 已单独写入 | 设备拥有自己的网络配置 | TLS、ASR/TTS/OpenClaw 已能工作 |
| 设备连到 WSS | 设备与 Gateway 的网络链路已通 | 舵机动作、语音效果和生产安全已验收 |

当前本机已验证过 Gateway 的 14 项 Python 单元测试，以及 V3 固件的主机测试。这个证据说明软件基线可测，不包含真实 ASR/TTS/OpenClaw、TLS 部署或物理舵机动作的验收。

## 9. 建议的下一步

先把 V3 源码整理成私有 Git 仓库，再在新电脑按本文完成 Gateway 测试、本地 OpenClaw 配置和 ESP-IDF 构建。等这三层稳定后，再按实际选择配置 TLS、ASR/TTS/Agent Adapter，并在安全条件下接入 `RobotDriver` 和真实机器人。

需要更完整地查看哪些内容属于原版、哪些属于 V3，以及哪些文件该带走或排除时，阅读 [项目范围与跨设备快速复现指南](../sesame-robot-v3-scope-and-replication-guide.md)。
