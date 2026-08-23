# Research: 本地端侧 ASR/TTS 的运行时与封装方式

> **Date:** 2026-07-22
> **Bead:** unavailable (`bd` is not installed)
> **Status:** Complete

## Summary

Sesame Robot 的 ASR/TTS 应运行在端侧电脑的常驻推理进程中，不运行在 ESP32，也不应直接写进 FastAPI WebSocket handler。对当前 Apple M1 Max 32 GB，第一版建议以 `sherpa-onnx + 中文流式 Paraformer/Zipformer INT8` 做 ASR，以 `sherpa-onnx + Kokoro multilingual` 做轻量 TTS；通过 Provider 接口接入 Voice Gateway，后续可不改会话层地替换为 NVIDIA GPU 上的 CosyVoice。

## Key Findings

### 1. 端侧电脑承担推理，ESP32 只承担实时音频 I/O

> **Confidence:** high — 项目两份技术文档给出一致边界。

项目已将 ESP32 的职责限定为 I2S、Opus、WSS、jitter buffer 和播放；ASR、Agent、TTS 在电脑或服务器侧完成。[P1] 当前音频契约为 16 kHz、16-bit、mono PCM，网络侧为 20 ms 一包的 Opus。[P1]

ASR/TTS 属于 Voice Gateway 的逻辑数据面，但不要求与 FastAPI 处于同一进程。模型加载耗时、推理阻塞和崩溃隔离都要求它们至少是独立 worker；以后需要 GPU 或多设备并发时，再把 worker 提升为独立本地服务。

### 2. 当前 M1 Max 的 ASR 首选是真正流式的 ONNX 方案

> **Confidence:** high — sherpa-onnx 官方文档同时列出 macOS arm64、流式 WebSocket/Python API 和多种中文在线模型。

对当前 Apple M1 Max 32 GB，首选 `sherpa-onnx` CPU provider，加中文/中英双语 streaming Paraformer 或 Zipformer INT8。官方文档列出了 macOS arm64 支持、流式 WebSocket server、中文 online Paraformer/Zipformer 及 INT8 模型。[S1]

FunASR 的 `paraformer-zh-streaming` 是第二选择。官方示例按音频 chunk 维护 cache，并给出 16 kHz mono 输入；官方 runtime 也提供 CPU 两遍式实时服务和 WebSocket client。[S2][S3] 它的中文 VAD、标点、热词生态更完整，但依赖和服务部署更重。

Whisper/faster-whisper 更适合离线转写或句末二次校正，而不是第一版的 partial-result 主链路。原因不是准确率，而是其常见“实时”实现多为滑动窗口重复解码，不等同于有状态在线 Paraformer/Zipformer。

### 3. TTS 分轻量端侧与高自然度 GPU 两档

> **Confidence:** high — sherpa-onnx 与 CosyVoice 官方文档分别覆盖轻量 ONNX 中文模型和模型级双向流式 TTS。

第一版在 M1 Max 上建议 `sherpa-onnx + kokoro-multi-lang-v1_1`。官方 TTS 模型列表包含中英双语 Kokoro、多种中文 VITS/MeloTTS/Matcha 模型和 Python API。[S4] 这类轻量模型适合先验证完整链路，但通常是按句子或短语生成后再切 PCM chunk，并非模型级双向流式。

如果目标是更自然的中文音色、情绪控制、声音克隆和更低首包延迟，应使用 CosyVoice 作为第二阶段方案。官方仓库声明支持 text-in/audio-out bi-streaming，并给出最低 150 ms 的首包指标；官方部署示例提供 gRPC 与 FastAPI，但容器命令使用 NVIDIA runtime。[S5] 因此 CosyVoice 不应作为当前 M1 Max 第一版的无压测默认项，更适合独立 NVIDIA GPU 推理节点。

### 4. 封装边界应是 Provider，而不是模型 SDK

> **Confidence:** high — 当前项目已定义 ASR/TTS Protocol，并明确禁止供应商 SDK 进入 session orchestration。

现有 `AsrProvider`/`StreamingAsrSession` 已覆盖 `push_pcm`、partial/final events、结束 utterance 和 close；`TtsProvider` 已定义返回裸 PCM s16le chunks 和取消信号。[P2] 这两个接口应保留。

推荐进程拓扑：

```text
ESP32
  ⇅ WSS: Opus 20 ms packets
FastAPI Voice Gateway
  ├─ Opus decode/encode、VAD、session、backpressure、interrupt
  ├─ ASR Provider client ⇄ ASR worker（模型常驻）
  ├─ Agent Adapter ⇄ OpenClaw
  └─ TTS Provider client ⇄ TTS worker（模型常驻）
```

单机 MVP 可以用 multiprocessing + 本机 Unix domain socket；为了接口稳定和以后迁移 GPU，直接采用以下服务协议更合适：

- ASR：双向流式 gRPC 或本机 WebSocket。请求连续发送 PCM s16le；响应连续返回 `partial/final`。
- TTS：gRPC server-streaming 或 HTTP chunked response。请求发送文本、voice、generation_id；响应只返回裸 PCM chunk。
- 健康检查：`/health/live` 只检查进程，`/health/ready` 必须确认模型已加载。
- 取消：每个 TTS/Agent run 都绑定 `generation_id` 和 cancel；打断时停止生成并丢弃旧 generation。
- 并发：模型实例常驻；单实例设置有界请求队列和并发上限，不能每个请求重新加载模型。

### 5. 音频格式只在 Voice Gateway 统一

> **Confidence:** high — 项目接口和网络协议已经固定。

ASR worker 只接收解码后的 PCM，不接收 Opus。[P2] TTS worker 优先返回模型原生采样率的裸 PCM；Voice Gateway 统一重采样到 16 kHz、按 640 bytes/20 ms 切帧，再编码为 Opus。[P1][P2] WAV header 不能作为 PCM 发送。

## Comparisons

| 部分 | 当前 M1 Max 32 GB 推荐 | NVIDIA GPU 质量档 | 不建议作为第一版默认 |
|---|---|---|---|
| ASR | sherpa-onnx + streaming Paraformer/Zipformer INT8，CPU | FunASR streaming 或较大 ASR 模型，需实测 | Whisper large 作为主流式 partial 引擎 |
| TTS | sherpa-onnx + Kokoro multilingual，CPU | CosyVoice 独立服务 | 在 M1 上先解决 CosyVoice CUDA/依赖兼容 |
| 封装 | 独立常驻 worker + Provider client | 独立容器/服务 + gRPC | 在 WebSocket handler 内加载/调用模型 |
| 设备协议 | ESP32 ⇄ Gateway 继续使用 Opus/WSS | 不变 | 让 ESP32 直接连接各模型服务 |

## Codebase Context

- 目标链路和 Python/FastAPI 技术栈已写在 `docs/implementation/voice-gateway-backend.md:3-27`。[P2]
- 目录已经预留 `providers/asr` 与 `providers/tts`，但 `voice_gateway/` 实现、依赖、容器和测试都不存在。[P2]
- ASR/TTS Protocol 已在 `docs/implementation/voice-gateway-backend.md:190-253` 定义。[P2]
- 共享 ASR/TTS inference pool 的平台边界已在 `docs/technical-design/esp32-opus-openclaw-platform.md:180-192` 定义。[P1]
- 当前状态仍是设计骨架，尚未运行验证，因此所有性能结论必须在这台 M1 Max 和真实 ESP32 音频上压测。[P2]

## Recommendations

1. 第一版锁定 `sherpa-onnx` 作为 ASR/TTS 的共同本地运行时，减少部署变量。
2. ASR 先测一个中文 streaming Paraformer/Zipformer INT8；TTS 先测 Kokoro multilingual。
3. 新建两个独立常驻 worker，但通过 Provider 接口接入，不让模型 SDK 进入 session/turn manager。
4. 首轮验收只看真实链路：partial latency、ASR final latency、TTS first-audio latency、RTF、CPU/内存、打断后旧音频是否归零。
5. 轻量 TTS 音质不满足后，再增加 CosyVoice GPU provider；不要重写 Voice Gateway。

## Recommended Beads

`bd` 未安装，以下为待建任务：

- `Implement sherpa-onnx streaming ASR provider and worker`
- `Implement sherpa-onnx Kokoro TTS provider and worker`
- `Benchmark M1 Max voice pipeline with real ESP32 recordings`
- `Evaluate CosyVoice GPU provider after MVP baseline`

## Open Questions

- 第一版只需普通话，还是还要粤语/中英混说？
- 同时在线设备数是 1、10 还是更多？
- TTS 是否必须支持声音克隆、情绪和指定角色音色？
- 可接受的 ASR partial、ASR final 和 TTS 首包 P95 分别是多少？

## Refuted / Discarded Claims

- “Mac 上应默认用 whisper.cpp”：它对 Apple Silicon 优化很好，但本项目需要有状态 partial/final 流式接口；sherpa-onnx 更贴合当前协议，Whisper 可保留作句末校正。
- “ASR/TTS 必须在 FastAPI 同一进程”：项目只要求它们处于 Voice Gateway 逻辑链路，独立 worker 更符合共享推理池与故障隔离设计。
- “CosyVoice 官方 150 ms 等于当前 M1 Max 也能达到”：官方指标不是本机实测，且官方部署示例以 NVIDIA runtime 为主。

## 2026-07-23 Model Landscape Update

### Local ASR models

> **Confidence:** high — model names and capabilities were checked against official repositories/documentation.

| Model/family | Main strengths | Streaming | Sesame fit |
|---|---|---|---|
| sherpa-onnx streaming Zipformer/Paraformer INT8 | Lightweight Chinese/Chinese-English edge inference | Native online streaming | M1 MVP first choice |
| FunASR Paraformer-zh-streaming | Chinese, VAD, punctuation, hotwords, mature runtime | Native online streaming | Strong second choice |
| SenseVoiceSmall | Fast multilingual recognition plus emotion/audio-event tags | Usually VAD-segmented rather than primary online partial model | Useful auxiliary/final recognizer |
| Qwen3-ASR 0.6B/1.7B | 52 languages/dialects, unified offline/streaming, robust complex audio | Supported with official toolkit/vLLM | Better for NVIDIA GPU quality tier |
| Fun-ASR-Nano | Chinese/multilingual LLM-style ASR, hotwords and newer FunASR serving | Streaming service available | GPU quality/final-pass option |
| Whisper large-v3/turbo | Mature multilingual ecosystem, translation and language ID | Core model is sliding-window/offline; streaming uses wrappers | File transcription or final correction |

Qwen3-ASR officially releases 0.6B and 1.7B models, supports 52 languages/dialects and unified streaming/offline inference.[S6] FunASR currently integrates Qwen3-ASR, Fun-ASR-Nano, SenseVoice, Paraformer and Whisper variants.[S2]

### Local TTS models

> **Confidence:** high — availability and headline capabilities were checked against official repositories/model documentation; target-machine performance remains unverified.

| Model/family | Main strengths | Streaming | Sesame fit |
|---|---|---|---|
| Kokoro-82M multilingual/zh | Small, multi-speaker Chinese/English, CPU/MPS friendly | Segment/chunk generation, not full text-in bi-streaming | M1 MVP first choice |
| MeloTTS Chinese | Simple, lightweight, fixed voice, CPU friendly | Sentence-level external chunking | Low-resource fallback |
| Fun-CosyVoice3 0.5B / CosyVoice2 0.5B | Chinese quality, dialects, cloning, instruction/emotion control | True text-in/audio-out bi-streaming | NVIDIA GPU production candidate |
| Qwen3-TTS 1.7B VoiceDesign/CustomVoice | Voice design, cloning, instruction control, multilingual | Official models mark streaming support | GPU evaluation candidate |
| IndexTTS2 | Chinese/English zero-shot cloning, emotion and duration control | Primarily file/utterance inference in official examples | Dubbing/content, not first robot path |
| Fish Speech 1.5/S2 family | High-quality multilingual cloning ecosystem | Deployment-specific | Research/quality comparison candidate |

CosyVoice’s official repository recommends Fun-CosyVoice3-0.5B and documents bi-streaming.[S5] Qwen3-TTS officially releases 1.7B VoiceDesign and CustomVoice models with streaming support.[S7] IndexTTS2 focuses on expressive zero-shot synthesis and duration/emotion control rather than the simplest low-latency robot path.[S8]

### Cloud API choices

> **Confidence:** high — current model/API identifiers were checked against official vendor documentation on 2026-07-23.

| Provider | ASR examples | TTS examples | Notes |
|---|---|---|---|
| Alibaba Cloud Model Studio | Qwen3-ASR-Flash-Realtime, Fun-ASR realtime, Paraformer realtime | CosyVoice v3/v3.5, Qwen3-TTS realtime | Strong Chinese/dialect ecosystem and WebSocket paths |
| Tencent Cloud | realtime ASR `bigmodel` / `16k_zh_en` | conversational TTS `flow_02_turbo`, realtime WebSocket TTS | Chinese, Cantonese, IoT WebSocket integration |
| OpenAI API | `gpt-4o-transcribe`, mini, diarize, `whisper-1` | `gpt-4o-mini-tts`, `tts-1`, `tts-1-hd` | Simple unified API; `whisper-1` itself does not stream |
| Azure Speech | realtime/fast/batch/custom speech | Azure neural TTS voices | Enterprise speech platform and custom speech |
| ElevenLabs | Scribe family/API | Flash/Turbo/Multilingual/Eleven v3 | TTS/voice quality ecosystem; evaluate Chinese and pricing |

Alibaba Cloud currently lists realtime Qwen3-ASR-Flash, Fun-ASR and Paraformer plus CosyVoice/Qwen3-TTS realtime models.[S9] Tencent documents bidirectional WebSocket access for IoT ASR/TTS and separate realtime endpoints.[S10] OpenAI’s audio API currently lists GPT-4o transcription variants and GPT-4o mini TTS/legacy TTS models.[S11]

### Updated recommendation

For Sesame Robot, benchmark only three initial candidates per capability:

- ASR local baseline: sherpa-onnx streaming Paraformer/Zipformer INT8.
- ASR cloud baseline: Alibaba Qwen3-ASR-Flash-Realtime or Tencent realtime ASR.
- TTS local baseline: Kokoro multilingual/zh.
- TTS cloud baseline: Alibaba CosyVoice realtime or Qwen3-TTS realtime.
- GPU research track: Qwen3-ASR 0.6B and Fun-CosyVoice3 0.5B.

Do not evaluate every available model before the voice pipeline works. Use the same recorded Chinese command set and measure correctness, partial latency, final latency, TTS first audio, interruption, memory and cost.

## 2026-07-26 Local vs Cloud Deployment Decision

### Verdict

> **Confidence:** high — the recommendation follows the project's privacy boundary and current local/cloud runtime capabilities; final model quality still requires device-side benchmarking.

There is no single winner for both capabilities. Sesame Robot should make the decision separately:

1. **Fastest MVP:** cloud ASR + cloud TTS. This minimizes model deployment work and establishes a quality/latency baseline quickly.
2. **Recommended product default:** local ASR + cloud TTS, with an explicit local TTS fallback. Raw microphone audio stays local during normal operation, while cloud TTS supplies stronger voice quality and expressiveness. Generated reply text still leaves the device, so this is not a zero-data-egress design.
3. **Strict privacy/offline mode:** local ASR + local TTS. This gives the strongest control and predictable offline availability, but the project owns model quality, memory, lifecycle, concurrency and crash recovery.

Do not automatically fall back from local ASR to cloud ASR. That changes the data boundary by uploading microphone audio and therefore requires an explicit policy and user consent.

### Decision Matrix

| Criterion | Cloud | Local |
|---|---|---|
| First demo speed | Better | More integration and runtime work |
| Offline use | Unavailable | Better |
| Raw-audio privacy | Audio leaves the device for cloud ASR | Better control |
| Predictable network-independent latency | Depends on RTT and provider load | Better after local optimization |
| Model operations | Provider owns model serving | Project owns weights, runtime and recovery |
| ASR quality for dialect/noise | Often a strong baseline; must benchmark | Model-dependent; can tune command vocabulary/hotwords |
| TTS naturalness/voice design | Usually easier to obtain | Lightweight local models may sound less natural |
| Cost | Usage-based and easy at low volume | Hardware/engineering cost; marginal request cost can be lower |

### Evidence Update

- sherpa-onnx officially supports macOS/arm64, streaming ASR, TTS and multiple language bindings, so the current M1 Max can host a real local implementation rather than a mock.[S1]
- Alibaba Cloud currently documents WebSocket streaming ASR with streaming text output and bidirectional streaming TTS with PCM/WAV/MP3/Opus support.[S9]
- Alibaba Cloud bills speech components by usage, confirming that cloud deployment trades local model operations for recurring provider cost rather than eliminating cost.[S12]

### Recommended Validation Order

1. Implement both Provider contracts first.
2. Use cloud ASR/TTS to validate the complete ESP32 → Gateway → Agent → playback loop.
3. Add local ASR and compare against the same real robot recordings.
4. Keep cloud TTS as the quality baseline; add local TTS for offline/privacy mode.
5. Choose defaults only after measuring ASR command accuracy, P95 final latency, TTS first-audio latency, interruption latency, memory and per-1,000-session cost.

## Sources

- [S1: sherpa-onnx official documentation](https://k2-fsa.github.io/sherpa/onnx/index.html) — Primary/Official — accessed 2026-07-22 — macOS arm64、streaming server/API、中文 online models。
- [S2: FunASR official repository](https://github.com/modelscope/FunASR) — Primary/Official — accessed 2026-07-22 — Paraformer streaming chunk/cache 示例与模型许可说明。
- [S3: FunASR runtime quick start](https://github.com/modelscope/FunASR/blob/main/runtime/quick_start.md) — Primary/Official — accessed 2026-07-22 — CPU 两遍式实时服务和 WebSocket client。
- [S4: sherpa-onnx TTS documentation](https://k2-fsa.github.io/sherpa/onnx/tts/index.html) — Primary/Official — accessed 2026-07-22 — 中文 Kokoro、VITS、MeloTTS、Matcha 模型与 Python API。
- [S5: CosyVoice official repository](https://github.com/FunAudioLLM/CosyVoice) — Primary/Official — accessed 2026-07-22 — bi-streaming、first-packet 指标及 gRPC/FastAPI/NVIDIA 部署示例。
- [S6: Qwen3-ASR official repository](https://github.com/QwenLM/Qwen3-ASR) — Primary/Official — accessed 2026-07-23 — 0.6B/1.7B、52 languages/dialects、streaming/offline inference。
- [S7: Qwen3-TTS official repository](https://github.com/QwenLM/Qwen3-TTS) — Primary/Official — accessed 2026-07-23 — released 1.7B VoiceDesign/CustomVoice models and streaming support。
- [S8: IndexTTS2 official repository](https://github.com/index-tts/index-tts) — Primary/Official — accessed 2026-07-23 — zero-shot cloning、emotion and duration control。
- [S9: Alibaba Cloud speech model configuration](https://help.aliyun.com/en/model-studio/multimodal-app-configuration) — Primary/Official — accessed 2026-07-23 — realtime ASR/TTS model identifiers。
- [S10: Tencent Cloud AI speech overview](https://cloud.tencent.com/document/product/647/131325) — Primary/Official — accessed 2026-07-23 — IoT WebSocket streaming ASR/TTS and language/model coverage。
- [S11: OpenAI Audio API reference](https://platform.openai.com/docs/api-reference/audio/voice-consent-object) — Primary/Official — accessed 2026-07-23 — current transcription and speech model identifiers。
- [S12: Alibaba Cloud Model Studio product billing](https://help.aliyun.com/zh/model-studio/product-billing) — Primary/Official — accessed 2026-07-26 — ASR/TTS usage-based billing and component-level charges。
- [P1: Sesame Robot platform design](../../docs/technical-design/esp32-opus-openclaw-platform.md) — Project source — 2026-07-21 — 端侧边界、音频协议、共享推理池。
- [P2: Voice Gateway backend guide](../../docs/implementation/voice-gateway-backend.md) — Project source — 2026-07-22 — Provider 接口、PCM/Opus 处理和队列模型。
