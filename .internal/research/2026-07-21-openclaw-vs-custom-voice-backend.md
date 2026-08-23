# 调研：Sesame 语音机器人使用 OpenClaw vs 不使用 OpenClaw

> 日期：2026-07-21
> 状态：完成
> 场景：多台 ESP32 机器人；WebSocket + Opus；云端 ASR、LLM、TTS；每位用户拥有自己的设备

## 摘要

当前阶段建议不用 OpenClaw，但预留可替换的 `AgentAdapter`。先用自建 Voice Gateway 完成实时语音、打断、多设备鉴权和租户隔离；只有当长期记忆、多个外部工具、定时/异步任务和用户 skills 成为产品核心时，才为部分用户接入每用户/家庭独立的 OpenClaw Gateway。

## 共同架构前提

无论是否使用 OpenClaw，实时音频数据面都保持独立：

```text
ESP32 ⇄ WSS/Opus ⇄ Voice Gateway ⇄ ASR/TTS
                            ↓ final transcript
                       AgentAdapter
                            ↓ reply + allowed_actions
                       TTS / Control Adapter
```

OpenClaw 如果存在，只实现 `AgentAdapter`，不承载 Opus、I2S、ASR/TTS 和 WebSocket 音频连接。

## 方案 A：不用 OpenClaw

### 为什么用这个方案

- 当前核心链路只是听、答、说、打断和少量动作，自建状态机更直接。
- Voice Worker 可以统一取消 LLM/TTS、清空旧 generation，尾延迟更可控。
- 多用户共享 ASR/TTS/LLM GPU pool，不需要每用户常驻 Agent Gateway。
- 没有通用 shell、浏览器、文件和插件运行时，攻击面更小。
- 音频默认不落盘、短期内存处理和删除策略更容易证明。

### 优点

- 低延迟、故障链短、易于压测。
- 多租户鉴权、PostgreSQL RLS、Redis key namespace 可直接按 tenant_id 设计。
- ASR/LLM/TTS provider 可自由替换。
- 资源池化效率高，适合大量低活跃设备。

### 缺点

- 需要自建会话、短/长期记忆、工具 schema、审批、审计、prompt 版本和评测。
- 每接一个外部服务，都要自己实现 OAuth、权限、错误和数据隔离。
- 产品若演进成开放式个人 Agent，自建编排层可能重复 OpenClaw 能力。

## 方案 B：使用 OpenClaw

### 为什么用这个方案

- OpenClaw 已提供 session、memory、tools、skills、多 Agent 路由、cron 和多渠道接入。[S1][S2]
- 可以把 `robot.wave`、`robot.set_face` 等做成 typed tools，并通过 allow/deny policy 控制。[S3]
- Gateway 提供协议、事件、取消和会话生命周期，减少通用 Agent 平台开发量。[S4]
- 适合跨天记忆、多个工具、多步骤执行、提醒和主动任务。

### 优点

- Agent 功能上线更快。
- 工具、skills、记忆和会话模型相对完整。
- 自托管、MIT、多模型、多渠道。
- 后期连接日历、智能家居、搜索、消息等能力更容易。

### 缺点

- 官方安全模型要求互不信任用户使用独立 Gateway；session ID 不是租户授权边界。[S5]
- 每用户/家庭一个 Gateway 会增加容器、内存、volume、凭据、监控和升级成本。
- 官方 Fleet 多租户 cell 编排仍是 experimental，且不提供共享入口、自助平台、计费或多机控制面。[S5]
- sandbox 只隔离 Agent 工具执行，不能把共享 Gateway 变成多租户安全边界。[S6]
- 默认能力面远大于机器人需要，必须关闭 shell、文件、browser、cron、nodes 等非必要工具。[S3]
- 插件和 skills 增加供应链、prompt injection 和权限配置风险。
- 错误插入实时语音热路径会增加延迟与故障耦合。

## 带权重决策矩阵

评分 1–5，5 最好；权重针对当前“先量产语音闭环”阶段。

| 评估项 | 权重 | 不用 OpenClaw | 使用 OpenClaw |
|---|---:|---:|---:|
| 实时延迟与打断 | 20% | 5 | 4 |
| MVP 复杂度 | 15% | 5 | 3 |
| Agent 能力 | 15% | 2 | 5 |
| 用户安全隔离 | 15% | 4 | 3 |
| 规模化运维 | 15% | 4 | 2 |
| 单用户资源成本 | 10% | 5 | 2 |
| 后续扩展速度 | 10% | 3 | 5 |
| **加权总分** | **100%** | **4.05** | **3.45** |

## 成本与风险

| 项目 | 不用 OpenClaw | 使用 OpenClaw |
|---|---|---|
| 常驻资源 | 共享服务，低 | 每用户 Gateway，较高 |
| GPU | 共享 | 仍可共享 |
| 运维对象 | 按服务增长 | 按用户/家庭增长 |
| 攻击面 | API、DB、模型 | 再加 tools、plugins、workspace、Gateway |
| 泄漏风险 | SQL/缓存租户错误 | 再加 session/memory/mount 错误 |
| 开发成本 | Agent 能力越多越高 | Agent 开发低，平台运维高 |

## 推荐路线

### 阶段一：不用 OpenClaw

完成 WSS/Opus、流式 ASR/TTS、打断、设备鉴权、租户隔离、动作白名单、日志脱敏和删除策略。定义：

```text
respond(tenant_id, user_id, device_id, session_id, text, context)
  -> reply_text + allowed_actions
```

### 阶段二：小流量 OpenClaw 试验

```text
Agent Router
├── DirectLLMAdapter
└── OpenClawAdapter
```

用 feature flag 给少量用户开启，比较留存、任务完成率、延迟、成本和安全事件。

### 阶段三：Agent 价值被证明后规模化

- 每用户/家庭一个独立 Gateway cell。
- 自建控制面负责创建、升级、暂停、删除和健康检查。
- 独立 token、state、workspace、credentials、volume 和 network policy。
- 内置 sandbox 为第二层；禁止 elevated，只允许 Sesame typed tools。
- 空闲实例休眠或按需启动。

## 引入 OpenClaw 的转折条件

以下条件至少出现三项，再正式引入：

1. 用户需要跨天、跨设备长期记忆。
2. 每个用户连接多个外部服务。
3. 请求经常需要两个以上工具或多步骤执行。
4. 需要提醒、定时、异步任务或主动消息。
5. 需要用户自定义 skills。
6. 自建 Agent 编排复杂度已经超过 Voice Gateway。
7. Agent 能力带来的留存/付费足以覆盖每用户实例成本。
8. 团队有能力维护容器、密钥、安全更新和租户生命周期。

## 最终判断

现在采用“不用 OpenClaw，但预留 OpenClaw adapter”。OpenClaw 是后续 Agent 能力加速器，不是首版语音链路必需品。

## Sources

- [S1 OpenClaw 官方概览](https://docs.openclaw.ai/) — Primary
- [S2 OpenClaw Memory](https://docs.openclaw.ai/concepts/memory) — Primary
- [S3 Tools and custom providers](https://docs.openclaw.ai/gateway/config-tools) — Primary
- [S4 Gateway protocol](https://docs.openclaw.ai/gateway/protocol) — Primary
- [S5 Multi-tenant hosting](https://docs.openclaw.ai/gateway/multi-tenant-hosting) — Primary
- [S6 Sandboxing](https://docs.openclaw.ai/gateway/sandboxing) — Primary
- [FastAPI WebSockets](https://fastapi.tiangolo.com/advanced/websockets/) — Primary
- [PostgreSQL Row Security](https://www.postgresql.org/docs/17/ddl-rowsecurity.html) — Primary
- [NISTIR 8259A IoT Cybersecurity Baseline](https://nvlpubs.nist.gov/nistpubs/ir/2020/NIST.IR.8259A.pdf) — Primary
