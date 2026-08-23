# Research: Sesame 客户交付与无 App Wi-Fi 配网的证据核查

- 日期：2026-08-03
- 状态：完成
- 任务记录：未创建；本机未安装 `bd` 命令，无法建立 Bead。
- 核查对象：Git tag `v1.2.1`（commit `d2c0225`）及 Espressif ESP-IDF 官方文档。

## 结论

1. 现有 `v1.2.1` **不能**做到「客户拿到设备后只填写 Wi-Fi 就完成交付」。固件只会从本地头文件或 NVS 读取已经写入的 SSID、密码、设备标识、Gateway Token 和根证书；没有客户配网流程。
2. 之前提出的「设备开临时 Wi-Fi、浏览器页面输入 Wi-Fi、二维码/一次性密码」是一个**待实现的产品方案**，不是当前项目已具备的能力，也不能因为有密码就称为等价于 Espressif Security 2。本次撤回该等价说法。
3. Espressif 官方确实提供 BLE 或 SoftAP 的 Wi-Fi Provisioning，并提供 Security 2（SRP6a + AES-GCM）。但 Security 2 要求配网客户端执行对应的安全握手；官方资料没有证明普通浏览器页面天然具备该能力。
4. 当前正式 ESP-IDF 配置未启用 Secure Boot、Flash Encryption、NVS Encryption。若设备交付给可物理接触的客户，不能把当前配置当作量产安全基线。
5. 现有语音链路还依赖电脑端 Voice Gateway：ASR/TTS 在该网关进程中调用，OpenClaw 默认以 `127.0.0.1:18789` 连接。是否改为云端网关或随设备交付计算主机，是独立的产品架构决策；不能假定 ESP32 自身能替代该网关。

## 已核实的项目事实

| 结论 | 证据 | 可信度 |
|---|---|---|
| Wi-Fi 与设备凭据来自本地头文件或 NVS。 | `firmware/esp32_voice_idf/components/sesame_transport/device_config.cpp`：NVS keys 为 `wifi_ssid`、`wifi_pass`、`device_id`、`gateway_id`、`token`、`root_ca`。 | 高：源码直接证据 |
| 正式固件没有客户 SoftAP/BLE 配网实现。 | 同一固件源码未发现 `wifi_prov_`、`protocomm`、`WIFI_MODE_AP/APSTA`、BLE 或 Blufi 调用；传输组件依赖为 STA、mDNS、WebSocket 客户端等。 | 高：源码直接证据 |
| 旧 Arduino 工程虽有控制用 SoftAP/DNS portal，但 Wi-Fi 仍是编译时 SSID/密码，并非客户 Wi-Fi 配置。 | `firmware/arduino/Sesame_Robot_WiFi_Controller` 的 portal 路由只覆盖控制；站点 Wi-Fi 凭据为静态配置。 | 高：源码直接证据 |
| 音频/对话需要电脑 Voice Gateway。 | `gateway/apps/voice_gateway/src/sesame_voice_gateway/pipeline.py` 完成 Opus→PCM、ASR、OpenClaw、TTS、PCM→Opus；`config.py` 将 OpenClaw 限为 loopback 默认 `ws://127.0.0.1:18789`。 | 高：源码直接证据 |
| 当前正式固件未打开 Secure Boot、Flash Encryption、NVS Encryption。 | `firmware/esp32_voice_idf/sdkconfig` 中相应 `CONFIG_*` 均为 `not set`。 | 高：构建配置直接证据 |

说明：`dependencies.lock` 出现 provisioning 相关依赖不等于功能已实现；以实际源码调用为准。

## 官方事实与边界

### Wi-Fi Provisioning

- ESP-IDF 的 Wi-Fi Provisioning Manager 支持 SoftAP 或 BLE 作为传输，并在 Protocomm 上交换 Wi-Fi 凭据。
- Security 0 是无安全；Security 1 是 X25519 + PoP + AES-CTR；Security 2 是 SRP6a + AES-GCM。
- Security 2 的 Salt/Verifier 必须预先由用户名/密码生成；客户端需要完成安全握手。官方提供 Android、iOS 和 `esp_prov`（Linux/macOS/Windows）客户端。
- 所以：「打开浏览器连 SoftAP」可以是传输体验，但「普通网页天然实现 Security 2」没有官方证据。若要走该路线，必须自行实现受审查的浏览器端安全协议并做互操作与渗透测试。

来源：[ESP-IDF Wi-Fi Provisioning](https://docs.espressif.com/projects/esp-idf/en/v5.0.6/esp32s3/api-reference/provisioning/wifi_provisioning.html)，[Unified Provisioning](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-reference/provisioning/provisioning.html)。

### DPP / Easy Connect

- ESP32-S3 官方支持 DPP Enrollee，并可通过 QR Code 配网。
- 官方明确的免安装 App 范围只到「部分 Android 10+ 手机」；不能据此推断 iPhone 或所有 Android 设备均可用。
- WPS 也是无 App 的候选路径，但依赖客户路由器支持，且官方没有把它定义为与 DPP 或 Security 2 等同的安全方案。
- SmartConfig 不符合「无 App」要求，因为官方定义它由手机 App 广播 Wi-Fi 凭据。
- 所以这证明芯片/SDK 有此能力；**不证明**所有客户手机、路由器或操作系统都能无 App 完成配置。兼容性必须用目标机型和路由器实测。

来源：[ESP-DPP API](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/network/esp_dpp.html)，[Wi-Fi API（WPS）](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-guides/wifi.html)，[SmartConfig API](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/network/esp_smartconfig.html)。

### 量产时的本地凭据与固件保护

- Wi-Fi 驱动会把 SSID 和 passphrase 写入默认 NVS。官方建议在使用 Flash Encryption 时同时开启 NVS Encryption。
- Espressif 对生产用例建议同时启用 Flash Encryption 和 Secure Boot v2；Flash Encryption 应使用 Release 模式。Secure Boot 防止未授权代码启动，但不应单独使用。
- Secure Boot / Flash Encryption / ROM Download 与 JTAG 相关 eFuse 的配置包含不可逆步骤，并会改变维修、串口下载与升级流程。必须先完成真实 OTA、回滚、生产烧录与返修流程演练后才可熔断。

来源：[NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/storage/nvs_encryption.html)，[Security Features Enablement Workflows](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/security/security-features-enablement-workflows.html)，[Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/security/secure-boot-v2.html)，[Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/security/flash-encryption.html)。

## 不再采用的表述

| 旧表述 | 核查结论 |
|---|---|
| 「当前项目已有 captive portal，可让客户填 Wi-Fi」 | 不成立。旧 Arduino portal 是控制页，正式语音固件无配网 API。 |
| 「唯一 Wi-Fi 密码 + 二维码/Token 就等价 Security 2」 | 不成立。没有来自 Espressif 的等价依据。 |
| 「客户只连 Wi-Fi 即可，不需要 Gateway」 | 对当前架构不成立。当前 ASR/TTS/OpenClaw 链路依赖电脑 Voice Gateway。 |
| 「无 App 的网页配网已经足够安全」 | 信息不足，必须先实施并验收安全协议。 |

## 基于证据的下一步

1. 不把 `v1.2.1` 直接作为客户版交付。
2. 先作一个**不量产、不烧 eFuse**的配网 POC，对比：
   - 官方 SoftAP/BLE + Security 2 + 官方 `esp_prov` 客户端；
   - DPP QR Code；
   - 自研浏览器 SoftAP 配网（仅作为待验证方案，不提前宣称安全）。
3. 用目标客户手机、路由器、断网/重连、错误密码、重放和旁路监听场景记录通过率与失败原因，再决定客户体验。
4. 独立确定计算架构：云端 Gateway，或随产品交付电脑/网关主机。当前仓库不能证明任一方案已经可交付。
5. 量产前将 Secure Boot v2、Flash Encryption Release、NVS Encryption、TLS 证书验证、签名 HTTPS OTA、密钥保管、售后恢复流程做成可复现的工厂验收项。

## 研究限制

- 本文没有做硬件、手机或路由器实测，故未报告成功率、兼容率、延迟或安全强度数值。
- 本文不把设计推断写成事实；所有「建议」均明确标注为建议，所有「已具备」均以源码或官方文档为依据。
