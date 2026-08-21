# Sesame V3：交给 AI 的新电脑搭建交接文档

## 任务目标

在一台新电脑上，为当前仓库建立可验证的 Sesame V3 开发环境。完成标准是：Gateway 的依赖环境可重建，OpenClaw daemon 处于本机运行状态，ESP-IDF 主机测试与固件构建可执行；真实设备、TLS、DashScope 和 OpenClaw 的凭据只在操作者提供后配置。

不要把“能构建”误报成“实体机器人已完成语音闭环”。缺少硬件、凭据或网络时，应明确报告停在哪一层。

## 固定架构和目录

仓库根目录下的职责固定如下，不要创建并行的 Arduino、Gateway 或 OpenClaw 实现：

```text
firmware/esp32_voice_idf/  唯一正式 ESP32-S3 固件
gateway/                   唯一电脑端 Voice Gateway
contracts/                 跨组件协议与 JSON schema 的唯一来源
docs/                      部署、安全与硬件说明
ops/openclaw/              OpenClaw 2026.7.1-1 的安装入口
```

运行链路固定为：`ESP32-S3 -- WSS/Opus --> Voice Gateway -- ASR --> OpenClaw -- TTS --> Voice Gateway -- WSS/Opus --> ESP32-S3`。

OpenClaw 只接收 Gateway 已确认的最终文本。它不接收音频，不能持有 ESP32 token、TLS 私钥或 DashScope API Key，也不能直接控制舵机、GPIO、文件系统、浏览器、shell 或任意 URL。

## 开始前的只读检查

先执行，不修改文件：

```bash
git status --short
git remote -v
sed -n '1,240p' README.md
sed -n '1,260p' docs/run-gateway.md
sed -n '1,260p' docs/security.md
sed -n '1,260p' gateway/.env.example
```

若工作树已有修改，保留它们，不能使用 `git reset --hard`、`git checkout --`、删除目录或覆盖不属于本任务的文件。先把检测到的修改列出，再继续不冲突的环境安装工作。

## 安全规则

- 不读取、输出、提交或复制 `gateway/.env`、真实 `device-config.json`、`device-nvs.bin`、证书私钥、API Key、token、Wi-Fi 密码或完整 `~/.openclaw/`。
- 可读取 `gateway/.env.example`、`provisioning/device-config.example.json` 和 Git 跟踪的文档作为样例；不能假造真实值。
- 当需要真实凭据、私有 CA、TLS 证书、设备串口、Wi-Fi 或云端授权时，停在该步骤并向操作者逐项索取；不要静默降级为无 TLS、公开监听或虚构 Provider。
- OpenClaw 必须保持本机回环地址；Gateway 控制台也只允许本机访问。
- 模型输出只能请求固件白名单动作与表情；禁止用 AI 生成任意舵机角度、GPIO 指令或宿主机命令。

## 目标环境

| 区域 | 要求 |
| --- | --- |
| Gateway | Python 3.12、`uv`、系统 `libopus`。依赖锁定在 `gateway/uv.lock`。 |
| OpenClaw | Node.js 22 LTS 或 24，OpenClaw 固定为 `2026.7.1-1`。 |
| 固件 | ESP-IDF v5.5.4、ESP32-S3 工具链、Python 3、C++20 编译器、Node.js。 |
| 硬件基线 | ESP32-S3，16 MB Flash、8 MB OPI PSRAM；INMP441、MAX98357A、OLED 和独立舵机电源。 |

## 执行顺序

### 1. 建立 Gateway 依赖基线

```bash
cd <PROJECT_ROOT>/gateway
make sync
```

如失败，报告 `python3.12 --version`、`uv --version`、`pkg-config --modversion opus`（如适用）和完整的非敏感错误信息。不要手动编辑 `uv.lock` 或降低 Python 版本。

### 2. 安装 OpenClaw 本机运行时

先确认 Node.js 主版本为 22 或 24。安装命令有全局系统影响，只有在操作者授权本机安装后执行：

```bash
cd <PROJECT_ROOT>/ops/openclaw
sh install_openclaw.sh
openclaw daemon status
```

脚本会安装固定版本、注册并启动 macOS daemon。确认状态正常后，不要把 `~/.openclaw/` 回写到仓库。

### 3. 处理受保护配置

只有操作者提供实际值后，才可创建 `gateway/.env`：

```bash
cd <PROJECT_ROOT>/gateway
cp .env.example .env
chmod 600 .env
```

需要的值包括：设备 token 映射、DashScope API Key、`SESAME_ALLOW_REMOTE_SPEECH` 的明确同意、OpenClaw URL/token/会话密钥、TLS 证书/私钥/CA 路径和 mDNS 标识。写入后不要打印文件内容；只用变量名报告是否缺失。

### 4. 启动并验证 Gateway

```bash
cd <PROJECT_ROOT>/gateway
make run
```

另开终端检查：

```bash
curl -k https://127.0.0.1:8766/healthz
```

如果 TLS 已启用，以 `.env` 中配置的证书主机名确认控制台；不要忽略浏览器证书错误，也不要通过关闭 TLS 来让 ESP32 连通。

### 5. 建立固件构建基线

```bash
cd <PROJECT_ROOT>/firmware/esp32_voice_idf
source <ESP_IDF_V5_5_4>/export.sh
bash tests/run_host_tests.sh
idf.py set-target esp32s3
idf.py build
```

不使用旧电脑的 ESP-IDF 绝对路径。若 host tests 失败，定位到具体脚本或测试，不要跳过测试或删除现有测试文件。

### 6. 仅在设备与密钥齐备时写入 NVS

让操作者在仓库外的私密位置提供每台设备配置，再执行：

```bash
cd <PROJECT_ROOT>/firmware/esp32_voice_idf
python3 tools/generate_nvs.py \
  <PRIVATE_DEVICE_CONFIG_JSON> \
  build/device-nvs.bin
```

NVS 文件包含秘密，设为 `0600`。未确认串口和硬件接线前，不执行烧录。确认后才使用 `esptool.py --chip esp32s3 --port <PORT> write_flash 0x9000 build/device-nvs.bin`。

## 验收与汇报格式

完成或受阻时，用以下格式报告，并标明每项的同轮命令输出：

1. **仓库状态**：分支/远端、已有未提交修改，以及本次新增或修改的文件。
2. **Gateway**：`make sync`、`make run`、`/healthz` 的结果；若缺失 `.env` 则说明这是凭据阻塞，不称为启动成功。
3. **OpenClaw**：安装版本和 `openclaw daemon status`；不输出 token、会话或路径下的敏感文件内容。
4. **固件**：ESP-IDF 版本、host tests、`idf.py build` 的结果。
5. **实机**：NVS 是否生成/烧录、TLS/WSS 是否已验证、ASR/OpenClaw/TTS 是否已验证、白名单动作与急停是否已验证。没有同轮证据的一律写“未验证”。

相关事实以根目录 README、`contracts/`、[网关运行说明](../run-gateway.md)和[安全文档](../security.md)为准；若文档与代码不一致，先报告差异，不要自行选择其中一个悄悄改写。
