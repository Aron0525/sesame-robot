# Research: ESP32 → PC → openHAB 的消息处理与回复架构

> **Date:** 2026-07-27  
> **Status:** Complete  
> **Scope:** ESP32 连接电脑，由 openHAB 处理事件、编排并向设备回复；含机器人/执行器场景。资料以 2026-07-27 可访问的官方文档与标准为准。

## Summary

这类系统真正难的并不是“ESP32 能否把一条消息发到 openHAB”，而是断线、重启、重复投递、状态错觉、慢任务和危险动作同时发生时，仍能判断每条命令是否应执行、是否已经执行、以及如何安全恢复。推荐将 **MQTT over Wi-Fi** 用作 ESP32 与电脑/openHAB 的低频设备事件与控制总线；openHAB 负责设备状态、简单自动化和可视化，PC Gateway/外部服务负责协议校验、会话/LLM、长任务、幂等与审计。

对本仓库的语音机器人，我的结论更明确：**不要把 openHAB 放进 WSS 音频、TTS 打断或舵机实时控制闭环。** 当前权威架构的 Voice Gateway 已是该闭环的唯一入口；若需要 openHAB，应通过 MQTT 接入“状态、告警、低频触发和观测”这一旁路控制面。[S16]

## Key Findings

### 1. 传输通了不等于消息可靠：Wi-Fi、TCP 和 MQTT 都会暴露重连、重复、积压问题

> **Confidence:** high — Espressif、RFC 和 MQTT 标准对失败语义一致。

- Wi-Fi 断开时，ESP-IDF 的默认 LwIP 行为会中止所有 TCP socket，因此 TCP/WebSocket/MQTT 连接必须在 Wi-Fi/IP 恢复后重新建立，不能假设网络回来后旧连接继续有效。[S1]
- TCP 不保留应用层消息边界；直接 TCP/串口协议必须定义最大帧、长度/分隔符、版本、序号与校验，接收端按缓冲区累积解析，而不是把一次 `read()` 当作完整消息。[S2]
- MQTT QoS 0 可能丢失，QoS 1 仍可能重复；因此 QoS 不是“动作只执行一次”的承诺。控制命令需要 `commandId`、TTL、`bootId + seq`、设备端去重和业务 ACK。[S3]
- ESP-MQTT 发布可阻塞数秒，outbox 满会失败；网络发送须独立于采集/电机控制任务，使用有界队列并定义满载时丢弃低优先级遥测、保留最新状态、关键命令失败即告警的策略。[S4]

### 2. openHAB 最易踩坑是把命令、真实状态和事件混在同一 Item

> **Confidence:** high — openHAB 官方明确区分命令和设备状态，并说明 `NULL`/`UNDEF` 状态。

- `sendCommand` 代表“请求动作”，并不证明设备已执行；Item 的状态也可能受 `autoupdate` 预测影响。要证明机器人实际完成，必须由设备以独立 ACK/status topic 回报。[S5]
- openHAB Item 初始为 `NULL`，设备失联或 Binding 配置异常可变为 `UNDEF`；规则不能把这两种值当成普通业务状态。[S5]
- 对“每条上行消息都要处理”的入口使用 MQTT trigger 或 `received update` 并以 `requestId` 去重；对真正的状态迁移使用 `changed`。这是因为 state update 即使值没变也会产生事件，而 changed event 只在值实际变化时产生。[S6]

### 3. 重启恢复会制造幽灵动作，必须恢复工作流事实而不是盲目恢复设备状态

> **Confidence:** high — 官方 Persistence 与 Rules 文档直接说明默认不恢复、并行启动和恢复层级。

- 仅安装 persistence 并不会跨重启恢复 Item；需要显式配置 `restoreOnStartup`。[S7]
- Persistence 与 Rule engine 并行启动，规则可能早于状态恢复执行；而且恢复不准确的旧状态会触发不应发生的 rule action。[S7][S8]
- 应持久化 `requestId/messageId`、阶段（`RECEIVED/PROCESSING/SENT/ACKED/FAILED`）、尝试次数、截止时间和最后 ACK，而不要恢复“设备在线”“正在播放”“正在移动”等瞬时物理状态。启动到 start level 80/100 后再与设备重新对账。[S7][S8]

### 4. 复杂消息和慢任务塞进 openHAB Rules，会带来并发、定时器和解析的隐蔽故障

> **Confidence:** high — openHAB JavaScript/MQTT/JSONPath 文档明确记载这些边界行为。

- 同一 JS rule 文件默认共享执行上下文，同一时刻只能运行一条规则；把 HTTP/LLM/TTS 等慢 I/O 放进该文件会让其他事件排队。[S9]
- MQTT transformation 任一步失败/返回 null 会丢弃值；JSONPath 找不到匹配时会返回整个原始 JSON。复杂会话 payload 不应由 Binding transformation 直接拆成业务状态。[S10]
- 用短规则做“校验 → 记录工作流 → 投递外部服务 → 接收结果/ACK → 发布回复”；会话记忆、LLM、TTS、重试队列、超时补偿和数据库审计放在 PC Gateway/FastAPI 等外部服务。此为基于上述执行与解析边界得出的架构判断。

### 5. 下行“回复成功”必须以应用 ACK 定义，而不是以 MQTT publish 或 Item 更新定义

> **Confidence:** high — MQTT binding 提供 QoS、availability/LWT 和 retained，但这些并不表示设备完成了动作。

- openHAB MQTT binding 默认 QoS 为 0；连接丢失的默认探测/重连配置都是 60 秒。对机器人需要按可接受停机时间重新配置，但不能把值调到极小而制造重连风暴。[S11]
- availability topic 可以使用设备 LWT 表示 Thing 在线状态；单次 `speak/display/motion` 命令绝不能 retained，否则后来订阅者会再次收到旧命令。[S11]
- 下行 command 与设备 ACK 必须是不同 topic；重试复用同一个 `messageId`，设备端只执行一次。只有收到 `played/executed/failed` 的匹配 ACK，工作流才终态。

### 6. 安全边界不是 TLS 一项：openHAB 与 ESP32 都不能被当作可信的“直通管道”

> **Confidence:** high — openHAB、MQTT、Espressif、OWASP 的官方/标准资料相互印证。

- openHAB 默认允许 LAN 内用户访问 user APIs；官方还明确要求不能将 openHAB 直接暴露到公网。关闭 implicit user role，网关使用专用 API token，限制接口只对网关/loopback 可见；远程访问经 VPN 或带认证与 TLS 的反向代理。[S12][S13]
- MQTT 需按设备身份授权 Client ID 与可发布/订阅的 Topic；应使用 MQTTS、每设备独立凭据（可进一步采用 mTLS）和精确 Topic ACL，拒绝通配的写权限。[S14]
- 外部文本、MQTT payload 或 LLM 输出不能拼接为 Rule/Exec/shell；必须经过 schema 校验、命令白名单、状态前置条件、TTL、限幅/限频与设备 ACK。openHAB 的 action 能向外部硬件/服务发命令，也可执行命令行，故它不应成为不受控输入的执行器。[S15]
- 对会动的机器人，本地急停、看门狗超时停止、舵机限幅/限速、供电切断与碰撞/行程保护必须独立于 PC、Broker、openHAB 和网络。MQTT 的丢失/重复语义决定软件消息链路无法充当最后安全屏障。[S3][S15]

## Comparison

| 方案 | 优点 | 主要痛点 | 适用定位 | 结论 |
|---|---|---|---|---|
| USB Serial 直连 openHAB | 本地、调试直观、无需 Wi-Fi/Broker | 打开串口可触发 ESP32 reset；无内建 QoS/离线缓存；原始字节流需自定义 framing；业务数据与日志易混流。[S17] | 烧录、日志、人工维护、紧急诊断 | 不做主业务总线 |
| 直接 TCP/WebSocket | 路径短、无 Broker | 重连、粘/拆包、心跳、会话恢复、重复/ACK、多个消费者均需自建。[S1][S2] | 单设备、明确实时协议 | 对语音实时 WSS 可用；不要再让 openHAB 承担该协议 |
| MQTT over Wi-Fi | openHAB 原生 Binding，具备 topic、QoS、LWT、availability、多个消费者解耦。[S11] | QoS 0 丢失、QoS 1 重复、outbox 满、Topic/ACL 需治理。[S3][S4] | 状态、遥测、命令、ACK、自动化 | **ESP32 ↔ openHAB 的首选** |
| openHAB Rules 承担全流程 | 快速原型、设备可视化便利 | 慢 I/O 串行、定时器/重启/解析脆弱、难做可靠队列和审计。[S7][S9][S10] | 轻量路由、告警、简单编排 | 不承担对话/LLM/TTS 主流程 |
| PC Gateway/外部服务承担流程 | 可做 schema、数据库、幂等、并发、观测、测试 | 多一个服务和部署面 | 会话、LLM、语音、重试、复杂业务 | **生产/可维护路径** |

## Recommended Architecture

```text
ESP32
  ├─ telemetry / status / event / ack ── MQTTS ──> MQTT Broker
  └─ <── command（TTL、messageId、权限）────────────

openHAB
  MQTT Thing / Item（命令、真实状态、工作流状态分离）
  → 短规则：验证、状态更新、轻量自动化、告警/仪表盘
  ← 设备 availability / ACK

PC Gateway（唯一的复杂流程与执行边界）
  schema + device identity + per-device order + dedupe + audit
  → LLM / ASR / TTS / business service
  → Control Adapter（资产白名单、TTL、限幅、ACK 超时）
```

### 建议的 topic 和消息信封

```text
robot/{deviceId}/evt/request        # 上行请求；QoS 1
robot/{deviceId}/state              # 最新状态；审慎 retained
robot/{deviceId}/availability       # LWT：online/offline；retained
robot/{deviceId}/cmd/reply          # 一次性命令；QoS 1，绝不 retained
robot/{deviceId}/evt/ack            # received/played/executed/failed；QoS 1
robot/{deviceId}/diag               # 低优先级诊断；QoS 0
```

```json
{
  "schemaVersion": 1,
  "deviceId": "sesame-01",
  "bootId": "uuid-per-boot",
  "messageId": "uuid",
  "seq": 1042,
  "kind": "motion.play",
  "issuedAt": "2026-07-27T12:00:00Z",
  "expiresAt": "2026-07-27T12:00:05Z",
  "payload": {"actionId": "wave"}
}
```

设备只接受目标匹配、未过期、`messageId` 未处理、状态前置条件满足、且在动作白名单内的命令；随后回 ACK。未知版本、缺字段、重复、过期和越界命令一律拒绝并输出结构化错误码。

## Codebase Context

- 当前权威架构是 Wi-Fi + WSS，已明确删除 UART 业务链路，USB 只用于烧录/调试。[`docs/architecture/sesame-robot-v3-wifi-openclaw-architecture.md`](../architecture/sesame-robot-v3-wifi-openclaw-architecture.md)
- ESP32 只连接 Voice Gateway；OpenClaw 只处理最终文本和类型化工具调用，不能直接访问设备连接。[同上]
- 现有设计已要求“单一发送出口”，优先级为 `stop/flush > 控制 JSON > 音频`，这与本调研的 ACK、排队和安全结论一致。[同上]
- 旧三线计划保留了 UART/NDJSON、回执、超长/非法 JSON、断线和一小时稳定性验收等有价值的可靠性要求；但它已被 Wi-Fi/WSS 架构替代，不应重新作为当前主链路。[`docs/implementation/sesame-robot-v3-three-track-plan.md`](../implementation/sesame-robot-v3-three-track-plan.md)

## Recommendations

1. **先作边界决策：** 若目标是 Sesame 当前语音机器人，不新增 openHAB 到实时链路；保留 Voice Gateway → Control Adapter → ESP32 的 WSS 主路径。只有希望接入家庭自动化、仪表盘、通知或低频触发时，再以 MQTT 接入 openHAB。
2. **若采用 openHAB：** 先冻结 topic、JSON schema、QoS、retained 规则、LWT、`requestId/messageId`、ACK 和工作流状态表，再写 Rules/UI。
3. **将可靠性落到设备端：** Wi-Fi/IP/transport 显式状态机；指数退避+抖动；通信任务独立有界队列；上线发布状态快照；设备 `bootId + seq` 和 command 去重。
4. **将复杂业务留在电脑网关：** openHAB 规则只做短路径编排；LLM、对话、语音、数据库事务、长期 timer/重试放到可测试的外部服务。
5. **把安全与物理安全先做成不可绕过的边界：** MQTTS、设备独立身份/ACL、关闭 openHAB LAN 隐式权限、最小 token、严格 schema/白名单/TTL；本地急停和失联停机不依赖网络。

## Recommended Work Items

- `定义 MQTT 协议与 ACK 状态机` — P0；交付 topic 表、JSON Schema、QoS/retained/LWT 规范、重复/过期处理用例。
- `实现 ESP32 连接与命令可靠性层` — P0；交付 Wi-Fi/MQTT 状态机、有界队列、`bootId/seq/commandId`、LWT 与故障指标。
- `建立 PC Gateway 控制适配器` — P0；交付 schema 校验、动作白名单、TTL/去重、ACK 超时与审计日志。
- `以 openHAB 作为状态/自动化旁路接入` — P1；交付 Items/Things/Rules、持久化恢复协调器、仪表盘及故障告警。
- `安全上线基线与故障演练` — P1；交付 ACL、凭据管理、网络隔离、断网/重启/重复/过期命令/急停验收记录。

## Open Questions

1. 你想引入 openHAB 的具体目的是什么：家庭自动化集成、可视化、通知、还是希望它成为“机器人对话大脑”？前三类合理，最后一类不建议。
2. 目标是单机器人本地演示，还是未来多设备/远程访问？这决定 Broker 部署、mTLS、用户隔离、OTA 和审计的投入。
3. 动作风险等级如何划分？仅表情/播放与移动舵机/抓取应使用不同的 TTL、确认与硬件互锁要求。

## Sources

- **[S1]** [Espressif Wi-Fi Driver — reconnect and socket behavior](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi-driver/overview.html) — Official; Wi-Fi disconnect and TCP socket invalidation.
- **[S2]** [RFC 9293: Transmission Control Protocol](https://www.rfc-editor.org/rfc/rfc9293.html) — Standard; TCP/application message boundaries.
- **[S3]** [OASIS MQTT Version 5.0](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html) — Standard; QoS loss/duplicate semantics and authorization requirements.
- **[S4]** [ESP-MQTT](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/protocols/mqtt.html) — Official; publication blocking and full outbox behavior.
- **[S5]** [openHAB Items](https://www.openhab.org/docs/configuration/items) — Official; command/status distinction and `NULL`/`UNDEF`.
- **[S6]** [openHAB Event Bus](https://www.openhab.org/docs/developer/utils/events) and [Rules](https://www.openhab.org/docs/concepts/rules.html) — Official; update vs change event semantics.
- **[S7]** [openHAB Persistence](https://www.openhab.org/docs/configuration/persistence) — Official; persistence, restart and `restoreOnStartup`.
- **[S8]** [openHAB Rules Concepts](https://www.openhab.org/docs/concepts/rules.html) — Official; start levels and startup behavior.
- **[S9]** [openHAB JavaScript Scripting](https://www.openhab.org/addons/automation/jsscripting/) — Official; shared execution context and slow I/O concurrency.
- **[S10]** [openHAB MQTT Binding](https://www.openhab.org/addons/bindings/mqtt/) and [JSONPath Transformation](https://www.openhab.org/addons/transformations/jsonpath/) — Official; transformations, MQTT topic channels and errors.
- **[S11]** [openHAB MQTT Binding](https://www.openhab.org/addons/bindings/mqtt/) — Official; QoS, LWT, availability, reconnect, retained messages.
- **[S12]** [openHAB REST API](https://www.openhab.org/docs/configuration/restdocs) — Official; API privilege and token authentication.
- **[S13]** [openHAB: Securing Communication and Access](https://www.openhab.org/docs/installation/security.html) — Official; LAN access and remote exposure guidance.
- **[S14]** [OASIS MQTT Version 5.0](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html) — Standard; Client ID and Topic authorization.
- **[S15]** [openHAB Actions](https://www.openhab.org/docs/configuration/actions.html) and [OWASP IoT Top 10](https://owasp.org/www-project-internet-of-things/) — Official/industry baseline; external action and unsafe remote-control risk.
- **[S16]** [Current Sesame Wi-Fi/WSS + OpenClaw architecture](../architecture/sesame-robot-v3-wifi-openclaw-architecture.md) — Project design, 2026-07-26.
- **[S17]** [Espressif Boot Mode Selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html) and [openHAB Serial Binding](https://www.openhab.org/addons/bindings/serial/) — Official; serial-port reset behavior and raw serial constraints.
