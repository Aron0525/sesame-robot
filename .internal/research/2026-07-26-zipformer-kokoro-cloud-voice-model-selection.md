# Research: Streaming Zipformer、Kokoro 与云端音色模型选型

> **Date:** 2026-07-26
> **Bead:** unavailable (`bd` is not installed)
> **Status:** Complete

## Summary

`StreamingZIP Format` 应为 `Streaming Zipformer`，它是模型族而不是一个可部署的精确模型。`sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30` 与 `Kokoro-82M-v1.1-zh` 可以承担 Sesame Robot 的本地 MVP，但 Kokoro 只能切换 103 个预置说话人，不能根据任意参考音频克隆音色，也不是真正的双向流式 TTS。

若云端自托管并要求克隆音色，推荐 `Qwen3-ASR-1.7B` + `Fun-CosyVoice3-0.5B-2512`；若直接使用阿里云托管 API，推荐 `fun-asr-realtime` + `cosyvoice-v3.5-flash`。这里的“转换音色”按克隆/设计 TTS 音色理解；真正 speech-to-speech voice conversion 是另一条能力链。

## Key Findings

### 1. Streaming Zipformer 基本满足本地 ASR 主链路

> **Confidence:** high — sherpa-onnx 官方模型列表、API 和项目契约一致。

建议锁定：

```text
sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30
```

该模型是中文在线 Zipformer，主要模型文件约 160 MB；官方配置使用 16 kHz 特征、支持在线 endpoint、hotwords 和持续解码。[S1] sherpa-onnx 的在线识别结果包含 `is_final`，可以映射到项目的 partial/final 事件。[S2]

与 Sesame Robot 的映射：

```text
ESP32 Opus
  → Gateway 解码
  → PCM s16le / 16 kHz / mono
  → Streaming Zipformer
  → partial
  → endpoint / final
  → OpenClaw
```

限制：

- 中文 checkpoint 不应默认视为粤语模型；
- 若中英混说是硬要求，应另测 bilingual Zipformer；
- 若普通话、粤语和英文都要覆盖，优先评估官方 trilingual streaming Paraformer 或云端 Qwen/Fun-ASR；
- 远场、扬声器回声和多人噪声下的准确率不能由官方样例推断，必须使用真实 ESP32 录音压测。

### 2. Kokoro 满足轻量中文 TTS，但不满足任意音色克隆

> **Confidence:** high — Kokoro 模型卡与 sherpa-onnx 转换版给出了明确的语言、说话人和采样率。

精确模型关系：

```text
原始模型：hexgrad/Kokoro-82M-v1.1-zh
sherpa-onnx 包：kokoro-multi-lang-v1_1
```

它支持中文和英文，共 103 个固定 speaker，其中 55 个中文女声、45 个中文男声，固定输出 24 kHz。[S3] 模型采用 Apache-2.0 许可。[S4]

适配 Voice Gateway 时必须执行：

```text
Kokoro float samples / 24 kHz
  → 16 kHz 流式重采样
  → PCM s16le
  → 640 bytes / 20 ms 切帧
  → Opus
```

sherpa-onnx 将 Kokoro 归为 `OfflineTts`，不是模型级 text-in/audio-out 双向流式 TTS。[S5] 但其生成 API 提供增量 audio callback，callback 返回 0 可以停止继续生成，因此通过短句切分、独立 worker、`generation_id` 和队列清空仍可实现机器人打断。[S3]

音色能力边界：

| 能力 | Kokoro-82M-v1.1-zh |
|---|---|
| 选择预置音色 | 支持 |
| 混合已有 voice tensor | 可做，但不是克隆 |
| 上传参考音频克隆说话人 | 不支持 |
| 语音到语音音色转换 | 不支持 |
| 情绪/角色指令控制 | 不属于其主要能力 |

### 3. 这组本地模型适合 MVP，不应直接认定为生产最终方案

> **Confidence:** high — 功能契约匹配，但项目没有真实模型基准测试。

| Sesame 需求 | Zipformer | Kokoro | 判断 |
|---|---|---|---|
| 本地 M1 Max | macOS arm64 支持 | macOS arm64 支持 | 满足运行条件 |
| 16 kHz mono PCM | 直接适配 | 需从 24 kHz 重采样 | 可适配 |
| ASR partial/final | 支持 | 不适用 | 满足 |
| TTS PCM chunks | 不适用 | callback + adapter | 有条件满足 |
| 打断 | stream reset | callback cancel + queue flush | 有条件满足 |
| 中文 | 中文 checkpoint | 中英 | 满足普通话 MVP |
| 任意音色克隆 | 不适用 | 不支持 | 不满足 |

项目当前的初始目标包括 ASR final 100–400 ms、TTS first audio 150–500 ms，以及打断时清空旧 generation。[P1][P2] 这两个模型尚未在真实 ESP32 音频上通过这些指标，因此只能标记为“候选可行”，不能标记为“已经满足”。

### 4. 云端自托管推荐 Qwen3-ASR + Fun-CosyVoice3

> **Confidence:** medium-high — 官方能力完整，但 GPU、并发和端到端延迟仍需实测。

主推荐：

```text
ASR: Qwen3-ASR-1.7B
TTS: Fun-CosyVoice3-0.5B-2512
```

Qwen3-ASR-1.7B 支持 30 种语言和 22 种中文方言，统一支持 streaming/offline，并提供 vLLM 异步服务和流式推理。[S6] 如果成本和吞吐比最高准确率更重要，可将 ASR 降为 `Qwen3-ASR-0.6B`。

Fun-CosyVoice3-0.5B-2512 支持多语言/跨语言 zero-shot voice cloning、18+ 中文方言、文本输入与音频输出双流式，以及语言、方言、情绪、速度和音量指令；官方报告最低 150 ms 级生成延迟，但不能把该值当作 Sesame Robot 端到端实测结果。[S7]

部署边界：

```text
Voice Gateway
  ├── CloudSelfHostedAsrProvider → Qwen3-ASR service
  └── CloudSelfHostedTtsProvider → Fun-CosyVoice3 service
```

ASR 与 TTS 应分别部署、扩缩容和故障隔离。官方没有给出这对模型的统一最低显存，不能把经验估算写成硬件保证。

### 5. 托管 API 推荐 Fun-ASR Realtime + CosyVoice v3.5 Flash

> **Confidence:** high — 阿里云当前官方文档列出了实时协议、音色能力和区域。

北京区主推荐：

```text
ASR: fun-asr-realtime
TTS: cosyvoice-v3.5-flash
```

Fun-ASR Realtime 通过 WebSocket 实时接收音频并输出 interim/final 结果，支持热词、上下文增强和多种中文方言，适合机器人名称、技能名和产品词汇。[S8]

CosyVoice v3.5 Flash 支持实时流式 TTS、音色克隆、音色设计和指令控制；使用 Flash 作为低延迟起点，音质不满足再对比 Plus。[S9] v3.5 当前存在区域限制，部署前必须按机器人实际所在地核对北京/新加坡可用模型和网络 RTT。

如果只需要“给参考音频并克隆该声音”，还可使用：

```text
qwen3-tts-vc-realtime-2026-01-15
```

它是专门的实时 voice cloning 系列；不要把名称中的 `VC` 自动解释成任意 speech-to-speech voice conversion。[S10]

## “转换音色”的四种含义

| 用户说法 | 实际能力 | 对应模型 |
|---|---|---|
| 切换音色 | 从固定 voice/speaker ID 中选择 | Kokoro |
| 音色克隆 | 输入目标说话人录音，再用该音色朗读新文本 | Fun-CosyVoice3、Qwen3-TTS Base/VC |
| 音色设计 | 用文字描述创造新角色音色 | CosyVoice v3.5、Qwen3-TTS VD |
| 语音到语音转换 | 保留原语音节奏/内容，直接换说话人音色 | 独立 voice-conversion 能力，不等于普通 TTS |

本次推荐默认用户需要的是“音色克隆或音色设计”。如果需求是真正语音到语音转换，应单独评估 voice conversion Provider，不应塞进当前 `TtsProvider(text → audio)`。

## 2026-07-26 Local Accuracy and Cloud Fallback Gate

### Zipformer ASR does not have one universal accuracy number

> **Confidence:** high — the official icefall result table reports CER for the upstream streaming large checkpoint across multiple Chinese datasets.

For the streaming transducer large model that underlies the recommended ONNX checkpoint, the official results report the following character error rates (CER). `100% - CER` is shown only as a rough readability aid; CER is the correct metric and insertions mean it is not literally a classification accuracy.[S11]

| Official test set | CER | Rough character correctness |
|---|---:|---:|
| AISHELL-1 test | 1.91% | 98.09% |
| AISHELL-2 test | 4.12% | 95.88% |
| MagicData test | 2.71% | 97.29% |
| WenetSpeech test meeting | 7.91% | 92.09% |
| WenetSpeech test net | 8.54% | 91.46% |
| AISHELL-4 test | 17.83% | 82.17% |
| AliMeeting test | 28.74% | 71.26% |

These numbers are not Sesame Robot results:

- they are upstream checkpoint benchmarks, not measurements of the INT8 sherpa-onnx export on the M1 Max;
- they use public datasets, not ESP32 microphones, the robot speaker, the room, AEC or the actual command vocabulary;
- quantization, endpoint settings, VAD, hotwords and audio preprocessing can change the result.

Therefore the defensible expectation is only:

- clean, close-range Mandarin may land around the mid/high-90% character correctness range;
- far-field, echo and multi-speaker conditions may fall substantially below 90%;
- the real default cannot be chosen until the same robot recordings are sent to both local and cloud Providers.

### Kokoro TTS has no credible single “output accuracy” percentage

> **Confidence:** high — the official model card provides model facts and samples but no representative Chinese intelligibility/MOS benchmark.

TTS should be split into four metrics:

1. text intelligibility: re-transcribe the generated audio and compute CER;
2. critical pronunciation: names, numbers, dates, English abbreviations and polyphonic characters;
3. naturalness: human MOS-style score;
4. interaction performance: first-audio latency and interruption residual.

The Kokoro model card says v1.1-zh is not a strict upgrade and was released after a short training run; it does not provide a production-wide Chinese accuracy or MOS percentage.[S4] Any single numeric “Kokoro accuracy” without the project's test set would be invented.

### Proposed acceptance gates

These are project decisions, not model-vendor claims:

| Capability | Keep local as default when | Switch cloud to default when |
|---|---|---|
| ASR | command exact-match ≥95%; critical commands ≥99%; CER ≤5%; ASR final P95 ≤400 ms | any critical gate fails on real robot audio |
| TTS | round-trip CER ≤2%; critical names/numbers pass 100%; human naturalness ≥4/5; first audio P95 ≤500 ms; interruption residual ≤300 ms | intelligibility, naturalness, latency or voice-cloning requirement fails |

Build an initial 200-utterance ASR set:

- 50 close-range clean commands;
- 50 far-range commands;
- 50 commands with robot playback/room noise;
- 50 names, numbers, English terms and ambiguous words.

Build a 100-sentence TTS set covering short replies, numbers, dates, product names, English abbreviations and polyphonic Chinese characters. Compare local and cloud using exactly the same inputs.

### Cloud fallback recommendation

Cloud should be an explicit configured Provider, not a silent per-request fallback:

```text
ASR_PROVIDER=local|cloud
TTS_PROVIDER=local|cloud
```

If local ASR fails the gate, use `fun-asr-realtime` as the cloud baseline because the official service supports 16 kHz PCM, WebSocket streaming, dialects, hotwords and context enhancement.[S8]

For cloud TTS:

- voice cloning from a target recording: prefer `qwen-audio-3.0-tts-flash` or `qwen3-tts-vc-realtime-2026-01-15`;
- voice design from a text description: prefer `cosyvoice-v3.5-flash`;
- preset voice only: compare the provider's standard realtime voices against Kokoro.

The Alibaba Cloud model matrix distinguishes voice cloning from voice design and lists separate model families for them.[S9] Local-to-cloud switching must require an explicit privacy policy because cloud ASR uploads microphone audio.

## Codebase Context

- 项目 ASR 接口要求 PCM chunks、partial/final、finish utterance 和 close。[P1]
- 项目 TTS 接口要求 `text`、`voice`、`sample_rate`、取消信号，以及异步返回裸 PCM chunks。[P1]
- 设备音频为 16 kHz、16-bit、mono，Opus 每包 20 ms。[P2]
- 目前 `voice` 仍只是字符串，没有定义音色注册、参考录音、授权、租户隔离和删除接口。
- 语言范围、并发数、CER/命令准确率以及最终 P95 阈值仍未锁定。

## Recommendations

1. 本地 MVP 锁定 `sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30` 与 `kokoro-multi-lang-v1_1`，不要继续使用模型族简称。
2. 将 Kokoro 定义为“固定音色离线档”，不要把它描述为声音克隆模型。
3. 自托管云质量档使用 `Qwen3-ASR-1.7B` + `Fun-CosyVoice3-0.5B-2512`。
4. 托管 API 快速上线档按“参考录音克隆音色”需求使用 `fun-asr-realtime` + `qwen-audio-3.0-tts-flash`；只有“用文字描述创造新音色”时才改用 `cosyvoice-v3.5-flash`。
5. 为声音克隆新增独立 enrollment（音色注册）流程，至少包含授权凭证、voice_id、tenant_id、来源模型、状态、删除和审计；Voice Gateway 只消费已经注册的 `voice_id`。
6. 用同一真实录音集测本地与云端：命令准确率/CER、partial 稳定性、ASR final P95、TTS first-audio P95、打断残音、30 分钟内存和单会话成本。

## 2026-07-26 Stability, Local Complexity, and Final Managed-Cloud Pair

### Cloud and local are stable in different ways

| Dimension | Local deployment | Managed cloud API |
|---|---|---|
| Service availability | Not affected by Internet or provider outages, but the local computer is a single failure point unless workers, watchdogs and auto-restart are added | The provider operates model servers, capacity and redundancy, but the application still depends on provider availability, regional endpoints and account status |
| Latency predictability | More predictable after hardware and concurrency are fixed; can still degrade from CPU contention, thermal throttling, memory pressure and sleep/reboot | Server compute is managed, but end-to-end latency additionally includes Internet RTT, jitter, TLS/WebSocket setup, packet loss and queueing |
| Operational burden | The project owns model download, runtime compatibility, upgrades, memory, monitoring and crash recovery | The provider owns model serving and upgrades; the project still owns client retries, timeout, reconnect, quota and API-version handling |
| Offline and privacy | Works offline and raw microphone audio can remain on the local computer | Requires network access and sends audio/text/reference-voice data to the selected provider region |
| Scaling | A single controlled robot is simple; more robots require more local capacity or a central server | Multi-device expansion is operationally easier, but rate limits and cost must be managed |

Therefore:

- for one Sesame Robot on a controlled M1 Max, local inference normally has more predictable latency and stronger offline continuity;
- for a fleet or public product, managed cloud normally has lower operational burden and easier capacity scaling;
- cloud is not “connection-proof”: the official real-time ASR guide explicitly requires client reconnection and heartbeats, and Model Studio applies account-level/model-level rate limits.[S8][S12]

### With voice cloning, local TTS is harder than local ASR

The baseline local ASR path is structurally narrow: accept fixed PCM chunks, maintain streaming recognizer state, emit partial/final text, run endpoint detection and close the stream. Its difficult production issue is acoustic quality—far-field noise, echo, AEC, VAD and microphone variation—not basic model packaging.

A local TTS service that must change to an arbitrary target voice needs more than synthesis:

- reference-audio validation and enrollment;
- speaker representation or voice ID lifecycle;
- consent, tenant isolation, deletion and audit;
- streaming generation, sentence chunking and interruption;
- resampling/PCM packetization;
- GPU or accelerator lifecycle, concurrency and quality-consistency tests.

If TTS only chooses among fixed preset speakers, real-world ASR tuning may be harder. Under the user's current requirement—reference-audio voice cloning—local TTS is the higher-complexity component.

### Final managed-cloud recommendation

Assuming “change timbre” means cloning a voice from a reference recording, use:

1. ASR: `fun-asr-realtime`
   - WebSocket streaming;
   - 16 kHz PCM support;
   - interim/final results, timestamps, hotwords and context enhancement;
   - stable alias currently mapped by the provider to a dated snapshot.[S8]
2. TTS: `qwen-audio-3.0-tts-flash`
   - WebSocket and HTTP;
   - reference-audio voice cloning;
   - instruction control for style/dialect;
   - custom voice enrollment, query and deletion.[S9][S10]

This pair keeps ASR, TTS, authentication, regions and billing within one managed platform, which reduces integration and operational surface area. It does not remove the need for Provider adapters, deadlines, reconnect/backoff, circuit breakers, rate-limit handling and explicit voice-consent records.

Capability boundary:

- reference recording → cloned voice: `qwen-audio-3.0-tts-flash`;
- text description → newly designed character voice: `cosyvoice-v3.5-flash`;
- input speech → same content with another voice: a separate speech-to-speech voice-conversion Provider, not ordinary TTS.

## Open Questions

- 第一版是否必须支持粤语和中英混说？
- “转换音色”究竟是预置音色切换、参考音频克隆、文字音色设计，还是 speech-to-speech conversion？
- 同时在线设备数和 GPU 成本上限是多少？
- 是否允许克隆真人音色；如何记录同意、撤回和删除？
- Zipformer 中文 checkpoint 的权重商用许可需要在产品化前单独确认。

## Sources

- [S1: sherpa-onnx Streaming Zipformer models](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html) — Primary/Official — accessed 2026-07-26 — 中文在线 checkpoint、模型大小、16 kHz 配置、endpoint 与 hotwords。
- [S2: sherpa-onnx C API](https://k2-fsa.github.io/sherpa/onnx/c-api/html/c-api_8h.html) — Primary/Official — accessed 2026-07-26 — streaming ASR result 与增量 API。
- [S3: sherpa-onnx Kokoro v1.1-zh](https://k2-fsa.github.io/sherpa/onnx/tts/all/Chinese-English/kokoro-multi-lang-v1_1.html) — Primary/Official — accessed 2026-07-26 — 103 speakers、24 kHz、回调和取消。
- [S4: Kokoro-82M-v1.1-zh model card](https://huggingface.co/hexgrad/Kokoro-82M-v1.1-zh) — Primary/Official — accessed 2026-07-26 — 82M、中文/英文、Apache-2.0 与模型限制。
- [S5: sherpa-onnx TTS C API](https://k2-fsa.github.io/sherpa/onnx/c-api/html/tts.html) — Primary/Official — accessed 2026-07-26 — Kokoro 属于 OfflineTts。
- [S6: Qwen3-ASR official repository](https://github.com/QwenLM/Qwen3-ASR) — Primary/Official — accessed 2026-07-26 — 0.6B/1.7B、语言/方言、streaming/vLLM。
- [S7: Fun-CosyVoice3 model card](https://huggingface.co/FunAudioLLM/Fun-CosyVoice3-0.5B-2512) — Primary/Official — accessed 2026-07-26 — zero-shot cloning、bi-streaming、方言与指令。
- [S8: Alibaba Cloud real-time ASR](https://help.aliyun.com/en/model-studio/real-time-speech-recognition-user-guide) — Primary/Official — accessed 2026-07-26 — Fun-ASR/Qwen/Paraformer、格式、热词和地区。
- [S9: Alibaba Cloud TTS model selection](https://help.aliyun.com/en/model-studio/tts-model/) — Primary/Official — accessed 2026-07-26 — 实时 TTS、voice cloning、voice design 与模型版本。
- [S10: Alibaba Cloud voice cloning](https://help.aliyun.com/en/model-studio/voice-cloning-user-guide) — Primary/Official — accessed 2026-07-26 — Qwen3-TTS VC、CosyVoice 音色注册、地区和输入要求。
- [S11: icefall multi-zh streaming results](https://github.com/k2-fsa/icefall/blob/master/egs/multi_zh-hans/ASR/RESULTS.md#multi-chinese-datasets-char-based-training-results-streaming-on-zipformer-large-model) — Primary/Official — accessed 2026-07-26 — Zipformer large streaming CER across AISHELL, MagicData, WenetSpeech, AISHELL-4 and AliMeeting。
- [S12: Alibaba Cloud Model Studio rate limiting](https://help.aliyun.com/en/model-studio/rate-limit) — Primary/Official — accessed 2026-07-26 — 账号级/模型级 RPM、TPM、RPS、TPS 限流和退避策略。
- [P1: Voice Gateway backend guide](../../docs/implementation/voice-gateway-backend.md) — Project source — Provider 契约与 PCM 处理。
- [P2: Sesame Robot platform design](../../docs/technical-design/esp32-opus-openclaw-platform.md) — Project source — 音频格式、延迟、打断与数据边界。
