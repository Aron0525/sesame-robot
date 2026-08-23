# 实时语音 Agent：低延迟且可靠的 OpenAI 工具链调研

- 日期：2026-08-07
- 状态：完成
- 场景：电脑网关将 ASR 最终文本发送给 OpenAI；模型可直接回答、联网搜索或调用工具；结果再转为语音/动作下发。
- 目标：把约 10 秒的端到端等待，优先压缩为“尽快得到正确的首句/首音频”，同时避免实时问题的意图、工具或槽位错误。

## 结论

**不要把所有请求都调成低思考作为主方案。** 最大的节省通常来自消除串行回合：把“改写查询 → 判断是否搜索 → 搜索 → 第二次模型总结”改成一次轻量结构化路由，再走直接数据 API 或单次工具调用。OpenAI 指出生成输出 token 往往是最大的延迟段；减少输入 token 的收益通常远小于减少输出 token。[Latency optimization](https://developers.openai.com/api/docs/guides/latency-optimization)

推荐主线是：**规则/轻量路由 + 严格结构化输出 + 专用实时 API + 低思考默认 + 条件升档 + 流式首句 + 可取消链路**。短期保留现有 ASR 文本链路；把 Realtime speech-to-speech 作为中期 A/B POC，而不是立即重构。OpenAI 将 speech-to-speech 定位为自然、低延迟的会话，而文本链适合可预测、易审计的既有工作流。[Voice agents](https://developers.openai.com/api/docs/guides/voice-agents)

本仓库的历史性能记录也支持优先拆串行路径：曾记录 ASR 约 2.0 秒、网页搜索约 4.5 秒，且出现“模型判断 → 搜索 → 模型整合”的多段串行调用，OpenClaw 阶段约 11.2 秒。见 `/Users/mac/Documents/sesame robot/.internal/research/2026-07-21-openclaw-vs-custom-voice-backend.md`。这是一份历史快照；开始改造前应重新按阶段测量。

## 先测量：不要把 10 秒全归因于“思考”

为每轮生成 trace id，并记录以下时间戳和 P50/P95，按 `route`、模型、工具、缓存命中分别聚合：

| 区段 | 起止 | 优先观察 |
|---|---|---|
| 端点检测/ASR | 用户停说 → ASR final | 静音阈值、尾延迟、重传 |
| 路由 | ASR final → route JSON | 规则命中率、模型 TTFT、槽位缺失率 |
| 模型 | 请求 → TTFT → 完成 | reasoning effort、输出 token、缓存 token |
| 工具 | start → end | DNS/连接、供应商、超时、缓存命中 |
| TTS | 首文本 → first PCM | 首句长度、合成队列、缓存 |
| 播放 | first PCM → 实际播放 | 客户端缓冲、打断清理 |

建议先定可验证的体验 SLO：非工具短问句以“ASR final 到首音频”计；需要实时工具的问句以“首个准确状态提示”和“最终首句”分开计。不要把流式或“我查一下”当作实际工具变快；它们只改善等待感。

## 推荐架构

```text
ASR final / 稳定 partial
  │
  ├─ 本地规则：明确设备命令、问候、固定短语、已有缓存命中
  │       │
  │       └─ 直接模板 / 本地工具
  │
  └─ 一次低延迟结构化路由（默认 low）
          │  {intent, freshness, route, slots, missing_slots, risk, effort}
          │
          ├─ chat / stable_qa       → 一次回答模型，1–2 句，流式 TTS
          ├─ weather                → get_weather 结构化 API → 校验 → 模板回答
          ├─ current_info / longtail→ web_search 或 provider-side search → 短结果回答
          ├─ local_read / action    → 对应窄工具；写操作按风险确认
          ├─ clarify                → 只问一个决定性问题
          └─ escalate               → medium reasoning / 多步编排
```

### 思考等级：默认低，按失败成本升档

- `minimal`：无歧义的问候、计时器、固定本地命令、缓存的只读结果。
- `low`（默认）：工具选择、检索判断、单步搜索、一般问答与简单规划。OpenAI 将低 effort 定位为兼顾速度和 token 使用、可用于 tool-use、planning、search 等任务。[Reasoning models](https://developers.openai.com/api/docs/guides/reasoning)
- `medium`：多约束、多步骤、槽位冲突、低置信路由、工具失败后重规划、重要写操作前的判断。
- 不为普通即时对话默认 `high`。生产语音可从 low 起步，再按复杂度、延迟容忍度与失败成本升档。[Realtime prompting](https://developers.openai.com/api/docs/guides/realtime-models-prompting)

升级条件必须由可验证信号驱动：缺失决定性槽位、多个意图、工具返回不完整/冲突、写入风险、或评测证明当前路径失效。不要只相信模型自报的 `confidence`。

### “问天气答非所问”的具体防线

1. 路由 JSON 中有 `intent=current_weather` 时，工具白名单只暴露 `get_weather`；不要让泛 Web Search 参与该路径。
2. 要求 `location`；用户没有给地点时，返回且只返回一个简短追问。默认地点只能在用户已显式配置且可审计时采用。
3. 服务端校验地点、日期/时间、单位枚举与必填返回字段；验证失败就追问或报告数据不可用，不补写事实。
4. 得到可信天气 JSON 后，用模板直接回答；不需要第二次 LLM。用户要求解释、比较或建议时，才将**受限的结构化结果**交给模型。
5. 最终回答带内部字段 `source=tool`；天气路由没有合格工具结果不得作为“当前天气”播报。

Open-Meteo 的预测接口接受经纬度和 `current` 等字段、返回 JSON，可作为原型候选；生产前应另行核对适用区域、数据源、SLA 和商业许可。[Open-Meteo Forecast API](https://open-meteo.com/en/docs)

## Web Search：何时该搜，何时不该搜

| 判定 | 路径 |
|---|---|
| 当前天气、新闻、价格、汇率、赛况、交通、营业时间、库存、实时日程，或用户明确要求核实 | 专用 API 优先；无专用 API/长尾问题才 Web Search |
| 写作、解释、稳定知识、用户已经提供的事实 | 直接回答，不搜索 |
| “天气怎么样”但无地点；“最近如何”但对象不明 | 只问一个决定性澄清问题 |
| 本机/设备状态 | 调指定本地只读工具，不把它改写成网页搜索 |

把“查询标准化”和“是否检索”合并进一次 JSON 路由调用。每增加一次请求都会增加一次网络往返；OpenAI 的 latency 指南也建议把连续模型调用合并或并行。[Latency optimization](https://developers.openai.com/api/docs/guides/latency-optimization)

工具层约束：简单轮次最多一个相关只读工具；每个工具有独立超时、取消传播和结果大小上限；独立且确有必要的只读调用才并行。工具 schema 使用严格模式，路由只看到本轮需要的工具。OpenAI 推荐 function calling 使用 `strict: true`，Structured Outputs 可约束 JSON Schema。[Function calling](https://developers.openai.com/api/docs/guides/function-calling) / [Structured outputs](https://developers.openai.com/api/docs/guides/structured-outputs)

## 延迟优化优先级

1. **先删除不必要的串行模型回合。** 一次路由，直接 API/工具，能模板化就不进行“工具结果 → 第二次模型总结”。工具调用通常包含模型、工具、再模型的回合；这是最值得压缩的主路径。[Function calling](https://developers.openai.com/api/docs/guides/function-calling)
2. **限制输出。** 直接答案 1–2 句；工具结果先答案后细节；使用紧凑 JSON。生成 token 通常是主要延迟，削减输出往往直接减少生成时间。[Latency optimization](https://developers.openai.com/api/docs/guides/latency-optimization)
3. **流式首句与 TTS。** 收到完整、安全的首句就开始 TTS，而非等待全文；慢工具/medium 路径可先播一句有信息量的状态提示。SSE streaming 能降低首 token 的等待感，但不能缩短工具本身。[Latency optimization](https://developers.openai.com/api/docs/guides/latency-optimization)
4. **缩短 turn completion。** 若“10 秒”从用户停止说话开始计，端点检测可能占显著比例。LiveKit 将 VAD 的用户活动检测、句末判断、抢跑和打断分别调节；VAD-only 最快但误切风险更高。[Turn tuning](https://docs.livekit.io/agents/logic/turns/tuning/)
5. **缓存与预热。** 复用 HTTP/模型连接、预热 worker；天气按地点/单位设短 TTL；固定播报语预合成 TTS。稳定 system prompt、工具 schema 和例子放前，动态用户文本/时间/工具结果放后，以提升 exact-prefix prompt cache 命中。OpenAI prompt cache 依赖精确前缀匹配。[Prompt caching](https://developers.openai.com/api/docs/guides/prompt-caching)
6. **上下文语义压缩。** 保留 rolling summary、长期偏好、当前任务状态、已验证工具事实和最近少量轮次；中断时只写入用户实际听到的 assistant 文本。不要 gzip/Base64 prompt，也不要每轮重放完整 ASR 历史。
7. **短问句可抢跑，长话禁用。** LiveKit 的预生成默认只预跑 LLM、确认句末后才启动 TTS，能降低误触发成本。[Preemptive generation](https://docs.livekit.io/reference/agents/turn-handling-options/)
8. **打断即取消。** 新语音到达时取消 LLM、HTTP 工具、TTS 和客户端旧音频缓冲；未播文本不写进对话历史。Pipecat 的上下文策略可作为参考。[Pipecat #1842](https://github.com/pipecat-ai/pipecat/issues/1842)

## 项目参考与可借鉴做法

| 项目 | 借鉴点 | 采用判断 |
|---|---|---|
| [OpenAI Voice Agents](https://developers.openai.com/api/docs/guides/voice-agents) | 文本链的可控性 vs Realtime 自然低延迟 | 先优化文本链；中期做 Realtime A/B |
| [OpenAI Latency Optimization](https://developers.openai.com/api/docs/guides/latency-optimization) | 输出 token、合并回合、并行、streaming | 直接应用于网关热路径 |
| [LiveKit Agents](https://docs.livekit.io/agents/logic/turns/tuning/) | VAD/语义句末、抢跑、可打断、首音指标 | 参考 turn 管理和取消状态机，不要求迁移 LiveKit |
| [Pipecat](https://docs.pipecat.ai/api-reference/server/utilities/turn-management/user-turn-strategies) | VAD 开口 + AI Smart Turn 结束；上下文只写已播内容 | 适合现有 ASR/TTS 两端的状态管理 |
| [TEN VAD](https://github.com/TEN-framework/ten-vad) | 边缘侧低成本 VAD，减少尾部静音 | 若瓶颈在 ASR final 前，做本机 POC |
| [LangGraph router](https://docs.langchain.com/oss/python/langchain/multi-agent/router) | 明确类别时的轻量、确定性路由；按需注入历史 | 适合把 route 作为显式网关层 |
| [OpenAI Agent Evals](https://developers.openai.com/api/docs/guides/agent-evals) | 按 trace 验证工具选择与端到端行为 | 用于低 effort 后的防回归 |
| [Moshi](https://github.com/kyutai-labs/moshi) | 原生全双工语音的长期探索 | 仅长期 POC；不直接替代带工具的业务链 |

## 三期实施

### 第 1 期：本周，低风险高收益

- 加完整分段埋点与 trace dashboard。
- 默认 low；设置直接回答的输出上限和 1–2 句策略。
- 一次严格 JSON router，合并“改写 + 是否搜索”。
- 天气改为结构化 API + 槽位校验 + 模板回答；Web Search 只作长尾 fallback。
- 建立 20–50 条真实 ASR 样本评测集；先 A/B，再扩大范围。

### 第 2 期：下一阶段

- SSE 流式 → 句级 TTS；实现 AbortSignal/取消传播、barge-in、旧音频缓冲清理。
- 加地点/单位、天气 TTL、工具超时、缓存命中统计。
- 推行 rolling summary + 已验证事实状态，移除完整历史重注入。
- 尝试短、稳定、低风险问句的 partial-ASR LLM 抢跑。

### 第 3 期：长期 POC

- 使用 Realtime speech-to-speech 与现有链式方案做 A/B；比较首音频、路由/工具正确率、打断恢复和单位成本。
- 仅当 Realtime 在关键指标上同时胜出才迁移；否则保留文本链的确定性工具网关。

## 回归评测集与验收

每条样本同时记录：route 正确、是否该搜、工具正确、槽位完整、工具事实匹配、播报无幻觉、P50/P95、取消是否生效。

至少覆盖：

1. “北京现在天气如何？”
2. “天气怎么样？”（无地点，只追问地点）
3. “今天和明天上海要带伞吗？”（天气工具且日期一致）
4. 稳定知识解释（不得搜索）
5. 用户明确说“帮我查一下今天的汇率”（必须实时源）
6. 搜索超时（不得凭旧知识冒充实时结果）
7. ASR 把地点修正的 partial 结果（丢弃旧推测）
8. 闲聊（低延迟直答）
9. 设备查询（仅本地工具）
10. 写操作（确认、幂等与中断边界）
11. 用户在 TTS 中插话（取消旧 LLM/HTTP/TTS/播放）
12. 工具返回地点与用户地点不一致（阻断播报并重试/澄清）

用独立的 route/trajectory 断言评估，而不是只人工听最终文案。OpenAI 的 agent evals 支持基于 trace 判断“是否选择了正确工具”。[Agent evals](https://developers.openai.com/api/docs/guides/agent-evals)

## 可直接交给 AI 的调研与方案提示词

```text
你是实时语音 Agent 系统架构师。请为一个“电脑网关 → ASR final 文本 → OpenAI/工具 → TTS/动作下发”的本地语音机器人，设计低延迟且可靠的改造方案。当前端到端约 10 秒；目标是在不牺牲回答相关性、工具准确性和可打断性的前提下，优先缩短“用户说完到首个可播放音频”的时间。

先用公开的一手资料调研并引用来源，重点参考 OpenAI（Latency Optimization、Voice Agents、Realtime、Function Calling、Structured Outputs、Prompt Caching、Evals）以及 LiveKit Agents、Pipecat、TEN VAD、LangGraph router。不要把“所有请求都调低思考”作为方案；不要输出私有思维链，只给可复核的结论和简短决策理由。

系统约束：保留现有 ASR 文本入口和网关控制能力；模型可直接回答、调用天气/本地工具或 Web Search；当前链路可能存在“模型判断 → 搜索/工具 → 模型整合”的串行往返。优先提出最小改造，再评估 Realtime speech-to-speech POC。

请输出：
1. 端到端时延分解与仪表盘：ASR final、路由、LLM TTFT/完成、工具 start/end、TTS first-audio、实际播放；分别给 P50/P95、超时和验收目标。
2. 项目对比表：每项做法减少哪一段时延、代价/副作用、是否适合当前文本链。
3. 二级路由与状态机，覆盖 LISTENING、PARTIAL、TURN_CONFIRMED、ROUTING、TOOL_RUNNING、SPEAKING、BARGE_IN、CANCELLED。快速路由必须一次返回严格 JSON：
   {intent, needs_fresh_data, route, slots, missing_slots, risk, effort}。
4. 推理等级策略：默认 low；仅无歧义本地命令/问候可 minimal；仅多步骤、歧义/冲突、工具失败重规划或重要写操作才 medium。说明每个升级条件如何在服务端验证。
5. 实时数据策略：天气、新闻、价格、汇率、赛况、交通、营业时间、库存、实时日程和用户明确要求核实时才检索。天气必须优先走结构化 get_weather API，禁止默认走泛 Web Search；缺地点时只问一个地点问题；校验 location/time/units 和工具必填字段后再回答。稳定知识、写作、解释、用户已提供的事实不搜索。
6. 工具策略：工具白名单、strict JSON schema、简单轮次最多一个相关只读工具、每个工具的 timeout/取消传播/幂等要求、何时并行、何时直接模板返回而不发第二次 LLM。
7. 上下文、流式和抢跑：rolling summary、长期偏好、当前任务、已验证工具事实；不重放完整历史；只把实际播出的助手文本写回历史。说明 partial ASR 的 speculative LLM 仅适用于哪些短、稳定、低风险问句，以及 barge-in 时需要取消哪些任务与缓冲。
8. 三期实施路线：本周、下一阶段、长期 POC。每项按预期收益、工程成本、风险排序。
9. 至少 12 条可自动化回归测试，特别覆盖：天气缺地点/有地点、普通闲聊、搜索超时、错误工具路由、ASR 中途修订、工具结果过期、地点不一致、用户打断和写操作确认。

输出要求：给出 Mermaid 流程图、紧凑伪代码、建议的 JSON Schema、指标定义和每个建议的验证方法。明确区分“实际时延降低”与“仅改善等待感”。所有关键结论附直接来源链接；把假设和需要 A/B 验证的项目单独标注。
```
