# 调研：ESP32-S3 Wi-Fi 间歇断连诊断与修复路径

> **日期：** 2026-07-29  
> **Bead：** 未创建（本环境未安装 `bd`）  
> **状态：** 完成  
> **范围：** Sesame Robot V3 当前 ESP-IDF 固件、Espressif 官方 SDK/硬件设计指南/勘误表，以及社区资料交叉核对；本轮不修改固件或硬件。

## 摘要

当前工程的首要问题不是芯片勘误或路由器：Wi-Fi 初次获得 IP 后，代码立即注销 `WIFI_EVENT` 处理器；此后任何 STA 断连都不会执行应用层重连。这足以解释“首次连接正常、运行一段时间后掉线且不恢复”。[S1]

应先修复为一个生命周期覆盖整个运行期的 Wi-Fi 状态机：记录断连 reason/RSSI/BSSID，有界指数退避重连，并在再次拿到 IP 后恢复 WebSocket。接着做 `WIFI_PS_NONE` 的 A/B 测试；若仍有断连，再依次排查 3.3 V 动态供电、天线/RF、路由器 2.4 GHz 与认证配置。[S2][S3][S4]

## 代码库结论

### P0：断连处理器在第一次连接后被注销

> **置信度：高。** 这是本地源代码的直接事实；不需要推测外部环境。

- `wifi_event()` 在 `WIFI_EVENT_STA_DISCONNECTED` 时调用 `esp_wifi_connect()`（[gateway_client.cpp](/Users/mac/Documents/sesame%20robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_transport/gateway_client.cpp:35)）。
- 但 `connect_wifi()` 在第一次 `IP_EVENT_STA_GOT_IP` 后返回前注销两个事件处理器并删除其 event group（[gateway_client.cpp](/Users/mac/Documents/sesame%20robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_transport/gateway_client.cpp:92)）。因此后续掉线再无该回调，也无 Wi-Fi 层重连。
- `kWifiFailed` 被等待但从未设置（[gateway_client.cpp](/Users/mac/Documents/sesame%20robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_transport/gateway_client.cpp:24)）；初次认证/找 AP 失败只能在 20 秒后笼统地超时。断开事件未读取 `wifi_event_sta_disconnected_t.reason`，也未记录 RSSI/BSSID 或重试数。
- 工程已有纯函数指数退避策略，但 Wi-Fi 与 WebSocket 没有采用；WebSocket 固定 1 秒自动重连（[gateway_client.cpp](/Users/mac/Documents/sesame%20robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_transport/gateway_client.cpp:183)）。

官方也说明：`WIFI_EVENT_STA_DISCONNECTED` 会清除 TCP/UDP 连接；应用应区分主动断开和异常断开，并在异常时安排重连，在 `IP_EVENT_STA_GOT_IP` 后再启动依赖 IP 的业务。[S2]

### 目标状态机

| 事件 | 必做动作 | 不应做的事 |
|---|---|---|
| `WIFI_EVENT_STA_START` | 首次调用 `esp_wifi_connect()` | 同时开始扫描 |
| `WIFI_EVENT_STA_DISCONNECTED` | 读取并记录 `reason`、RSSI、BSSID；清在线状态；关闭/标记失效业务 socket；用有界指数退避调度重连 | 在回调里无限立即重连；对应用主动断开重连 |
| `IP_EVENT_STA_GOT_IP` | 清零连续失败次数；记录 IP；恢复/启动 WebSocket | 复用掉线前的 TCP socket |
| 连续失败达到阈值 | 停止高频连接，间隔扫描并输出诊断 | 持续让连接占用扫描机会 |

`esp_wifi_connect()` 一次只发起一次连接尝试；`failure_retry_cnt` 可帮助处理建连失败，但“已发生断连后是否重连”仍须由应用策略决定。[S3]

## 官方诊断与修复建议

### 1. 先让日志能归因

> **置信度：高。** 官方 API 以 reason code 表示断开成因，现场缺少该信息就无法有针对性修复。[S2][S3]

建议在断开回调中记录：`reason`、`rssi`、BSSID、当前信道、连续重试数、`esp_reset_reason()` 与 WebSocket 事件/错误码。重点把日志按下表分类：

| 断开原因 | 优先排查 |
|---|---|
| `WIFI_REASON_BEACON_TIMEOUT` | RSSI、天线/外壳、2.4 GHz 干扰、AP 重启；不要先改密码 |
| `WIFI_REASON_NO_AP_FOUND` | 独立 2.4 GHz SSID、信道、隐藏 SSID 与配置阈值 |
| `WIFI_REASON_AUTH_FAIL`、`WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT` | 密码、WPA2/WPA3、PMF、路由器加密组合 |
| `WIFI_REASON_ASSOC_TOOMANY`、`WIFI_REASON_ASSOC_FAIL` | 路由器最大客户端数、ACL/接入策略 |
| `WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT` 且周期固定 | 路由器组密钥轮换与固件/认证兼容性 |
| 同时出现 reset/brownout | 3.3 V 动态电源问题，而非单纯 Wi-Fi |

### 2. 关闭省电做 A/B，不要当作无条件最终方案

> **置信度：高。** 默认 STA 省电模式为 `WIFI_PS_MIN_MODEM`；官方提供 `WIFI_PS_NONE` 来禁用 modem-sleep，代价是更高功耗。[S4]

对本项目的实时语音长连接，先在 `esp_wifi_start()` 后试验：

```cpp
ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
```

连续运行至少 24 小时，并与默认模式比较断连次数、reason、平均功耗和音频时延。若仅关闭省电后稳定，再将 AP 的 DTIM/省电交互作为主要方向；不要直接使用 `WIFI_PS_MAX_MODEM`，官方明确它会更容易丢失广播数据。[S4]

### 3. 固定一个兼容性基线再测路由器

> **置信度：高。** 当前代码要求至少 WPA2-PSK，PMF 为 capable 而非 required（[gateway_client.cpp](/Users/mac/Documents/sesame%20robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_transport/gateway_client.cpp:75)）。

- 用独立的 **2.4 GHz** SSID、非隐藏、WPA2-PSK/AES（CCMP）建立对照；不要用开放网络做常态配置。
- 将 2.4 GHz 固定到空闲的 1/6/11 之一，带宽先用 20 MHz；关闭 Mesh 的快速漫游/客户端引导后再比较。
- 若 reason 表示认证/握手失败，才查看 WPA3 transition、PMF、密码与路由器固件；`pmf_cfg.required = true` 不应作为未知旧 AP 的默认设置。
- 若断开频率恰好是固定周期，检查路由器的组密钥轮换和 DHCP 租约，不要用“加重连循环”掩盖它。

### 4. ESP32-S3 硬件检查（与本工程芯片匹配）

> **置信度：高。** 主工程设置的目标是 `esp32s3`；以下数值使用 S3 官方设计指南，而不是经典 ESP32 指标。[S5]

- 3.3 V 电源建议输出能力不低于 **500 mA**；主电源入口至少 10 µF，芯片 VDD3P3 在发射电流突增时官方建议额外 10 µF，并建议 LC 抑制高频谐波。[S5]
- 不要只用万用表看静态 3.3 V：用示波器在芯片近端、语音播放与持续 Wi-Fi 发送同时进行时测量。若日志有 brownout/reset，先修供电、公共地、LDO 和线束。
- 成品模块：将天线远离电池、金属支架、电机线与 USB/UART 线；先去壳、靠近 AP 做对照。自研 RF：50 Ω、短且不跨层的 RF 线、连续参考地、无临近高速信号，天线接入的匹配网络必须按本板测调。[S6]

## 芯片手册与勘误结论

> **置信度：高。** 已查看 ESP32-S3 最新勘误表摘要及 IDF 版本信息。

ESP32-S3 当前勘误表列出的项目为 cache、RTC/light-sleep、analog power、LCD、USB-OTG、RMT、touch 与 ADC；没有直接对应“Wi-Fi STA 随机断连”的条目。[S7] 因而在当前工程（ESP-IDF v5.5.4）中，**不应先把问题归为已知 S3 Wi-Fi silicon erratum**。若实际板卡为别的 ESP32 家族，必须重新按其型号和 revision 核对勘误表。

## 社区资料的作用与限制

> **置信度：低（仅作经验交叉核对）。**

检索到的 CSDN 案例集中在 USB 供电不稳、天线/干扰、信道选择及断线重连。它们与官方建议方向一致，但文章的“稳定性提升百分比”和具体电容/布局数值缺乏可迁移测试条件，不能作为本项目的验收证据。[S8] 本项目应以 reason code、实测 RSSI、供电波形和断 AP/重启 AP 的复现结果做判断。

## 建议执行顺序

1. **修 P0 固件状态机。** 将 Wi-Fi handler 作为 `GatewayClient` 的长期成员；异常断开后用指数退避重连；`GOT_IP` 后恢复 WebSocket；为 reason/RSSI/BSSID/reset reason 加日志。
2. **真机故障注入。** 分别测试断 AP 电源 60 秒、重启 AP、弱信号、路由器重启与 24 小时稳定运行；每次保存日志和恢复耗时。完成标准是自动重新获 IP 并恢复 WSS，无人工重启。
3. **省电 A/B。** 默认 `WIFI_PS_MIN_MODEM` 对比 `WIFI_PS_NONE`，记录断连率、reason、功耗；仅在证据支持时保留关闭省电。
4. **硬件/网络对照。** 去壳近 AP、独立 2.4 GHz WPA2/AES SSID、停用蓝牙/电机做 A/B；若问题仍在，用示波器测 3.3 V 芯片近端并审查天线区域。

## 开放问题

- 尚未取得真实串口日志及 `reason`，无法在“信号/认证/AP/供电”四类中最终归因。
- 尚未确认实际模组型号、天线结构、供电拓扑与路由器型号/设置。
- 本轮仅调研与审查，没有修改固件；P0 状态机仍待实现和真机验证。

## 来源

- [S1 本地 Sesame V3 GatewayClient 源码](/Users/mac/Documents/sesame%20robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_transport/gateway_client.cpp) — 本地代码 | 2026-07-29 核对 | 事件处理器生命周期、无 reason 日志、WSS 重连配置。
- [S2 ESP-IDF Wi-Fi Driver Overview](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi-driver/overview.html) — 官方 SDK 文档 | accessed 2026-07-29 | 断开事件语义、LwIP socket 清理、`GOT_IP` 时机。
- [S3 ESP-IDF Wi-Fi API Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html) — 官方 SDK API | accessed 2026-07-29 | `esp_wifi_connect()` 单次尝试、`failure_retry_cnt`、reason code 与省电 API。
- [S4 ESP-IDF Wi-Fi Performance and Power Save](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi-driver/wifi-performance-and-power-save.html) — 官方 SDK 文档 | accessed 2026-07-29 | 默认 modem-sleep、`WIFI_PS_NONE` 的效果和功耗取舍。
- [S5 ESP32-S3 Hardware Design Guidelines: Power Supply](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/esp-hardware-design-guidelines-en-master-esp32s3.pdf) — 官方芯片硬件指南 | accessed 2026-07-29 | 3.3 V/500 mA、近端去耦、发射突发电流。
- [S6 ESP32-S3 Hardware Design Guidelines: RF Checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html) — 官方芯片硬件指南 | accessed 2026-07-29 | 50 Ω、匹配网络、天线和 RF 走线约束。
- [S7 ESP32-S3 Series SoC Errata Summary](https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32s3/02-errata-summary/index.html) — 官方芯片勘误 | accessed 2026-07-29 | 当前 S3 勘误分类和受影响 revision。
- [S8 CSDN：ESP32 连接 WiFi 总是掉线？](https://blog.csdn.net/2401_89700177/article/details/145964294) — 社区经验 | accessed 2026-07-29 | 供电、天线、重连的经验性线索；不作为承重证据。
