# Sesame V3：换电脑后的搭建指南

> 本文写给在新电脑继续开发的人。先把电脑端服务和工具链建立起来，再连接实体机器人；不要一开始就把硬件、网络、证书和云端凭据问题混在一起排查。

## 1. 先准备代码与软件

克隆本仓库后，确认以下目录存在：

```text
sesame-robot/
├── firmware/esp32_voice_idf/  # 唯一正式固件
├── gateway/                   # 电脑端语音网关
├── contracts/                 # 协议与 schema
├── docs/                      # 部署、安全、硬件文档
└── ops/openclaw/              # OpenClaw 固定版本安装入口
```

安装以下软件：

| 用途 | 需要的软件 |
| --- | --- |
| Gateway | Python 3.12、`uv`、系统 `libopus`、浏览器。 |
| OpenClaw | Node.js 22 LTS 或 24、项目固定的 OpenClaw `2026.7.1-1`。 |
| ESP32 固件 | ESP-IDF v5.5.4、ESP32-S3 工具链、Python 3、C++20 编译器和 Node.js。 |
| 实机联调 | ESP32-S3（16 MB Flash、8 MB OPI PSRAM）、数据 USB 线、INMP441、MAX98357A、扬声器、独立舵机电源。 |

不要复制旧电脑的虚拟环境、ESP-IDF `build/`、`managed_components/` 或整个 `~/.openclaw/`。这些内容要么可重建，要么带有本机凭据和会话。

## 2. 建立 Gateway 本机基线

在不连接 ESP32 的情况下，先安装 Gateway 依赖：

```bash
cd <项目目录>/gateway
make sync
```

`make sync` 固定使用 Python 3.12 和 `uv.lock`。如果失败，先确认 `python3.12 --version`、`uv --version` 和系统 `libopus`，不要手工修改锁文件来绕过错误。

复制配置样例：

```bash
cp .env.example .env
chmod 600 .env
```

在 `.env` 中由你填写或从受控密钥存储取得以下真实信息：设备 token 映射、DashScope API Key、OpenClaw token、OpenClaw 会话密钥、TLS 证书与私钥路径，以及 mDNS/Gateway 配置。真实密钥绝不贴入终端记录、聊天内容或 Git。

启动前，确认云端音频授权是你主动同意的：`SESAME_ALLOW_REMOTE_SPEECH` 控制用户音频和待合成文字是否能发送到 DashScope。默认的监控台不显示转写和合成文本；临时调试后应把 `SESAME_DASHBOARD_DEBUG_CONTENT` 设回 `false`。

## 3. 安装并启动 OpenClaw

OpenClaw 运行在电脑上，只处理 Gateway 传来的文本；它不能直接连接 ESP32 或读取 Gateway 的云端 API Key。

```bash
cd <项目目录>/ops/openclaw
sh install_openclaw.sh
openclaw daemon status
```

安装脚本会全局安装固定版本并在 macOS 注册、启动 daemon。完成后，在 `gateway/.env` 设置 `SESAME_OPENCLAW_URL`、`SESAME_OPENCLAW_TOKEN` 和 `SESAME_OPENCLAW_SESSION_KEY_SECRET`。OpenClaw 应只监听 `127.0.0.1:18789`，不要把它暴露到局域网或公网。

## 4. 配置 TLS 并启动 Gateway

语音固件只连接 TLS Gateway。先根据[网关 TLS 要求](../gateway-tls.md)准备私有 CA、服务器证书和私钥，再在 `.env` 中配置其路径，并保证 ESP32 将同一根证书写入 NVS。

启动 Gateway：

```bash
cd <项目目录>/gateway
make run
```

健康检查：

```bash
curl http://127.0.0.1:8765/healthz
```

实际启用 TLS 时，必须按成套版本访问：仓库 v1.6 默认是 `https://sesame-gateway.local:8765/console`，当前实体机的 0821 组合是 `https://sesame-stream-gateway.local:8766/console`。控制台仅允许在运行 Gateway 的本机浏览器中访问；以固件协议、`.env` 和 Gateway 启动日志的共同结果为准。

如果要让 Gateway 在登录后自动恢复，参见 [`ops/macos/README.md`](../../ops/macos/README.md)。在 Gateway 直接占用 ESP32 USB 串口时，不要并行运行 `idf.py monitor`、Arduino Serial Monitor 或 `screen`。

## 5. 构建 ESP32-S3 固件

先加载新电脑上的 ESP-IDF v5.5.4 环境，不要照抄旧电脑的绝对路径：

```bash
cd <项目目录>/firmware/esp32_voice_idf
source <ESP-IDF-v5.5.4 路径>/export.sh
bash tests/run_host_tests.sh
idf.py set-target esp32s3
idf.py build
```

主机测试使用 Python、C++20 和 Node.js 验证协议、录音状态、动作/表情策略和网页控制；它通过不代表真实设备已连接。固件目标为 ESP32-S3、16 MB Flash、8 MB OPI PSRAM。

固定接线如下。舵机必须使用独立 5–6 V 电源，并与 ESP32 共地；不要用 USB 给八路舵机供电。

| 信号 | GPIO |
| --- | ---: |
| INMP441 SCK / I2S0 BCLK | 14 |
| INMP441 WS / I2S0 WS | 47 |
| INMP441 SD | 48 |
| MAX98357A BCLK / I2S1 BCLK | 1 |
| MAX98357A LRC / I2S1 WS | 2 |
| MAX98357A DIN | 3 |
| 舵机 S0–S7 | 4、5、6、7、10、11、12、13 |
| OLED SDA / SCL | 8 / 9 |

## 6. 为每台设备生成私密 NVS

复制样例到仓库之外的受保护目录，填写这台设备自己的 Wi-Fi、设备 ID、device token、网关与根证书配置：

```bash
cd <项目目录>/firmware/esp32_voice_idf
source <ESP-IDF-v5.5.4 路径>/export.sh
python3 tools/generate_nvs.py \
  <私密目录>/device-config.json \
  build/device-nvs.bin
```

然后在确认串口后烧录固件与 NVS。NVS 起始地址为 `0x9000`：

```bash
esptool.py --chip esp32s3 --port <串口> write_flash \
  0x9000 build/device-nvs.bin
```

`device-nvs.bin` 包含敏感信息。设置为仅自己可读，例如 `chmod 600 build/device-nvs.bin`，并按设备资产流程保管或销毁。不要提交、上传或发送它。

## 7. 按层验证

按下列顺序检查，失败时只排查当前层：

1. `make sync` 成功，Gateway 依赖可建立。
2. `openclaw daemon status` 正常，且只监听本机回环地址。
3. Gateway 的 `/healthz` 成功，控制台可由本机浏览器安全访问。
4. 固件主机测试和 `idf.py build` 成功。
5. ESP32 使用私有 CA、device token 和 mDNS 找到 Gateway，并建立 WSS。
6. 在明确同意云端语音处理后，验证 ASR → OpenClaw → TTS 的完整单轮对话。
7. 先测试 `stop`，再测试白名单中的 `rest`、`stand`、`wave`；确认机械限位、急停与断电方案有效。

问题分别参考[网关运行说明](../run-gateway.md)、[服务发现](../service-discovery.md)、[硬件模块清单](../hardware_modules.md)和[安全与隐私边界](../security.md)。
