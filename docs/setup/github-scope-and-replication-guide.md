# Sesame V3：GitHub 范围与跨设备复现

> 面向需要判断“GitHub 上有什么”和“换一台电脑还缺什么”的开发者。本文以本仓库为准，不把运行中的本机状态当成已提交代码。

## 结论

当前 GitHub 仓库包含 Sesame V3 的正式固件、电脑端 Voice Gateway、共享协议、部署文档和固定版本的 OpenClaw 安装入口。因此，克隆仓库可以得到 V3 的代码与可重建的配置样例。

但 GitHub 不包含任何真实凭据、设备私密配置、证书私钥、云端账号、OpenClaw 会话或已安装运行环境。它们必须在新电脑和每台设备上重新准备。克隆成功不等于实体机器人已经能语音对话。

## GitHub 包含的内容

| 内容 | 仓库位置 | 用途 |
| --- | --- | --- |
| 正式 ESP32-S3 固件 | `firmware/esp32_voice_idf/` | 音频、WakeWord、Opus、WSS、网页控制、OLED、舵机与 NVS 工具。 |
| 电脑端 Voice Gateway | `gateway/` | ASR → OpenClaw → TTS 编排、设备 WSS、控制台、mDNS 与安全策略。 |
| 共享协议 | `contracts/` | 音频包、JSON 事件、Agent 请求/响应 schema。 |
| 运行与安全文档 | `docs/` | 网关启动、TLS、服务发现、硬件和隐私边界。 |
| OpenClaw 安装入口 | `ops/openclaw/` | `openclaw@2026.7.1-1` 和 macOS daemon 安装脚本。 |
| Gateway 常驻服务入口 | `ops/macos/` | macOS `launchd` 服务安装说明。 |

## GitHub 不包含的内容

| 不包含项 | 原因与处理方式 |
| --- | --- |
| `gateway/.env`、DashScope API Key、OpenClaw token、会话密钥 | 凭据；从 `gateway/.env.example` 新建本机文件，并由部署者安全提供真实值。 |
| 设备 token、Wi-Fi 密码、私有 CA、TLS 私钥 | 每台设备和每个部署环境不同；保存在受保护的密钥管理或私密目录。 |
| `device-config.json`、`device-nvs.bin`、本地 `local-device-config.h` | 含设备凭据；用样例与生成工具在每台设备重新创建。 |
| `gateway/.venv/`、ESP-IDF 的 `build*/`、`managed_components/` | 本机依赖和构建产物；根据 `uv.lock`、`dependencies.lock` 和工具链重建。 |
| `~/.openclaw/`、会话、sandbox、个人记忆 | 本机运行状态和用户数据；不要复制整个目录或提交到仓库。 |
| `output/`、`training/`、`endpoint-gateway/` | 本地输出、训练过程或被 `.gitignore` 排除的材料，不是正式交付基线。 |

## 换电脑时应带走什么

推荐直接克隆当前私有仓库，再单独带走下列受控材料，而不是压缩整台旧电脑的工作目录：

```text
必须从 Git 得到
├── firmware/esp32_voice_idf/
├── gateway/（含 uv.lock 与 .env.example，不含 .env）
├── contracts/
├── docs/
└── ops/

由安全渠道单独提供
├── Gateway 的 .env 实际值
├── 私有 CA、TLS 证书与私钥
├── 每台 ESP32 的 Wi-Fi、设备 ID、device token
└── DashScope 与 OpenClaw 的账号/凭据授权
```

如果目标电脑必须离线构建 ESP-IDF，可把 `managed_components/` 作为一次性的加密离线包携带；它不应进入日常 Git 历史。Python 虚拟环境和 `build/` 不建议复制，因为它们与操作系统和本机路径绑定。

## 快速复现顺序

1. 克隆仓库并阅读根目录 README、`docs/run-gateway.md` 和 `docs/security.md`。
2. 安装 Python 3.12、`uv`、系统 `libopus`，在 `gateway/` 执行 `make sync` 建立依赖基线。
3. 安装 Node.js 22 LTS 或 24，并运行 `ops/openclaw/install_openclaw.sh`；确认 OpenClaw daemon 只监听本机回环地址。
4. 在安全位置创建 `gateway/.env`，配置真实 token、DashScope、OpenClaw、TLS 与 mDNS 值；不要把文件提交。
5. 安装 ESP-IDF v5.5.4，构建 `firmware/esp32_voice_idf`，并为每台设备生成独立 NVS。
6. 先验证 Gateway 健康检查和 OpenClaw daemon，再做 ESP32 的 TLS/WSS、ASR/TTS、动作和打断联调。

完整操作见[换电脑后的搭建指南](new-machine-setup-guide.md)。需要把任务交给 AI 时，使用[给 AI 的新电脑搭建交接文档](ai-agent-new-machine-handoff.md)。

## 不要混淆的边界

- 这个仓库是当前 V3 方案的代码库；它不同于仅提供原版机械结构和 Arduino 基线的上游 Sesame 项目。
- `OpenClaw` 已被纳入 V3 的运行链路，但它的安装结果和用户状态不在 Git 中。仓库只保存固定版本和安装入口。
- `make run`、控制台可打开或固件能构建，只说明对应层次可用；不证明 TLS、云端 ASR/TTS、OpenClaw 凭据、实体音频接线与舵机动作已经全部联调成功。
