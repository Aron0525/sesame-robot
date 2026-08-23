# 调研：Sesame Robot 的语音唤醒与自动结束录音

> **日期：** 2026-07-29  
> **状态：** 完成  
> **调研目的：** 用低操作成本的语音交互替换“按一次开始录音、再按一次结束录音”，并评估其在当前 Sesame Robot 中的可行性。  
> **说明：** 当前环境未提供 `bd` 命令，因此未创建或关闭 bead；其余研究流程、代码库核查和持久化文档均已完成。

## 摘要

消费级语音产品的稳定模式不是仅靠“提示词”：它们通常以唤醒词或手动入口开始一轮采音，以端点检测自动判断用户说完，并在回答后提供有限的免唤醒追问窗口。对 Sesame，推荐采用 **本地 WakeNet 唤醒词 + VAD 静音端点 + 按钮兜底**；先保持半双工，播放 TTS 时不做语音打断。

当前项目已有 INMP441 采集、20 ms PCM/Opus、WSS、`listen.start` / `listen.stop` 和设备轮次状态机，但运行代码仍是“两次按键切换录音”，且网关尚未消费控制事件或运行 Opus 解码、VAD、ASR、TTS。因此唤醒词不是开关配置，而是一个跨固件与网关的中等规模实现任务。[S13][S14]

## 关键概念：三个问题，三套能力

| 能力 | 回答的问题 | 不负责什么 |
|---|---|---|
| 唤醒词检测（Wake Word） | 机器人什么时候开始关注用户 | 不判断用户的一句话何时结束 |
| VAD（语音活动检测） | 当前音频帧是否存在人声 | 单独不能判定一句话已完成 |
| 端点检测（Endpointing / EoU） | 连续静音多久后可提交本轮 | 不决定是否应该唤醒 |

端点检测应是状态机规则：唤醒后，VAD 从“有人声”变成“静音”，并且静音累计超过阈值，才发出本轮结束。ESP-ADF 的 Recorder 将这一规则明确表述为静音超过 `vad_off` 时进入 VAD end。[S8] 因此，不能把 VAD 或 WakeNet 当成“自动说完检测”的同义词。

## 市场产品的实际交互方式

| 产品 | 开始一轮 | 结束一轮 / 会话 | 连续追问 | 对 Sesame 的启示 |
|---|---|---|---|---|
| Amazon Alexa / Echo | 唤醒词；PTT/TTT 也可开始 | 识别到用户说完，自动离开 Listening | Follow Up Mode | 最接近硬件机器人的基线：唤醒、端点、追问分别建模 [S1][S2] |
| Google Assistant / Nest | “Hey Google” | 停止说话或“Stop listening”等 | 回答后约 8 秒再听追问 | 追问窗口应有限、可见且可自然退出 [S3] |
| Siri / HomePod | “Siri/Hey Siri”；长按顶部可替代 | 公开用户文档未披露端点算法 | 未在本次来源确认 | 手动控制必须保留；状态灯覆盖 listening / thinking / responding [S4] |
| ChatGPT Voice | 用户主动点 Voice 图标进入持续会话 | 静音 / 退出控制结束会话 | 可实时打断 | 长会话对噪声、长停顿、他人说话敏感，不宜直接作为硬件机器人首版默认 [S5] |

Alexa 的开发者规范直接说明：Listening 可由 wake word、PTT 或 TTT 进入，并会在识别到用户说话结束时退出；开始和端点声音用于让用户确认是否正在听。[S1] 这支持一个结论：**“免第二次按键”是行业常规能力；“不再需要开始按键”则要额外增加唤醒入口。**

## 方案比较、难度与建议

| 方案 | 体验 | 技术难度 | 对当前项目的可行性 | 建议 |
|---|---|---:|---|---|
| 按一次开始 + VAD 自动结束 | 只消除第二次按键 | 低到中 | 可行，但当前网关也需补齐音频消费 | 用作第一步验收 |
| 本地 WakeNet + VAD 自动结束 | 免按键开始、免按键结束 | 中 | 可行；目标硬件 ESP32-S3 有官方能力 | **首版目标方案** |
| 回答后 5–10 秒追问窗口 | 少重复唤醒词 | 中 | 可行，需新增会话超时和反馈 | 在主链路稳定后增加 |
| 自定义中文品牌唤醒词 | 最自然的品牌体验 | 高 | 可行但依赖供应商训练、语料和验收 | 后置，不阻塞首版 |
| TTS 播放中可随时打断（barge-in） | 最自然 | 高 | 有条件可行；需要播放参考、AEC 和声学调试 | 二期专项 |

WakeNet9/9l 支持 ESP32-S3，且 WakeNet 已集成进 ESP-SR Audio Front End（AFE）；其输入要求为 16 kHz、单声道、16-bit PCM。[S6] VADNet 也在 AFE 内，官方示例使用 `vad_min_speech_ms=128` 与 `vad_delay_ms=128`，但 VAD 可能延迟 1–3 帧，必须接入 `vad_cache`，否则首字可能被裁掉。[S7]

自定义唤醒词不等于改一个字符串。Espressif 当前定制流程要求至少 20,000 条合格语料，训练和优化通常为 2–3 周；其效果又显著受麦克风、扬声器和腔体影响。[S10] 所以首版先验证官方现成词，硬件与声学稳定后再决定是否投入定制词。

TTS 播放时仍要监听并可靠打断，需把播放音频作为参考通道交给 AEC；ESP-SR 的全双工 AFE 管线确实支持 `AEC -> VAD -> WakeNet`，但单麦克风加播放参考的官方基准也需要约 778 KB PSRAM，双麦克风版本约 1.19 MB PSRAM，且真正难点是声学效果而非 API 接入。[S9] 因此第一版应半双工：说完后再回复，机器人说话期间通过按钮强制打断即可。

## 当前代码库核查

### 已具备

- `RecordingButton` 在 40 ms 消抖后用第一次按键开始、第二次按键停止，另有 30 秒上限；这是用户当前感到繁琐的交互。[S13]
- `VoiceController` 在 `button_.recording()` 时读取 INMP441 的 16 kHz / 20 ms PCM，编码 Opus 后通过 WSS 上传；`listen.start`、`listen.stop` 与 `asr.partial` / `asr.final` 协议名已经存在。[S13]
- 架构文档已预设 `IDLE → WAKE → LISTEN → THINK → SPEAK`，并明确规划 WakeNet 与 VAD 句末检测。[S14]

### 缺口

- 固件生产代码没有调用 ESP-SR / AFE / WakeNet / VAD API；闲置时也不读取麦克风，所以目前无法仅启用配置就获得唤醒词。[S13]
- `endpoint-gateway` 目前只验证设备认证、`session.hello` 和上行二进制帧；后续控制 JSON 被忽略，没有 Opus 解码、VAD、ASR、TTS 或 turn manager。[S15]
- 因而“仅在 ESP32 检测结束并停止上传”不能独立形成可用对话；网关还必须成为真实的流式 ASR 会话消费者。

## 推荐落地路径

### P0：先把第二次按键去掉（可独立验收）

1. 将按钮语义改成“手动发起本轮 / 强制结束或打断”，而不再是录音开关；第一次按后复用 `listen.start`。
2. 新增独立 `EndOfSpeechDetector`，持续消费现有 20 ms PCM；连续静音达到待测阈值后，复用 `listen.stop` 和 `LISTENING → THINKING` 转换。
3. 服务器按 `turn_id` 消费 `listen.start` / 音频 / `listen.stop`：解 Opus、流式 ASR、只将最终文本交给 Agent。服务器 VAD / 云 ASR endpoint 可以作为最终裁决，设备侧 VAD 用于提前停止上行。
4. 执行真实录音测试矩阵，而非固定拍一个“最佳静音阈值”：至少对 500、800、1200 ms 分别测短停顿误截断率、说完等待时间和无声误提交率。ESP-SR 官方没有给出适用于本机器人场景的通用结束阈值。[S7][S8]

**验收标准：** 用户按一次后正常说完，无须再按键；单句在静音后自动提交；短暂停顿不会频繁截断；按键随时可取消；无声和最长时长有明确反馈。

### P1：增加本地唤醒词，变为默认入口

1. 迁移闲置采音路径至 ESP-IDF / ESP-SR AFE；`IDLE` 状态持续读取 I2S PCM，但不上传环境音频。
2. 在 `IDLE` 运行 WakeNet；命中后发 `wake` 事件、亮起“正在听”状态、播放短提示音，并进入 `AWAKE`。
3. 在 `AWAKE/LISTEN` 运行 VAD；保存 VAD cache / 一小段 PCM 环形缓冲，再上传首个语音帧，防止唤醒词之后的第一个字被裁掉。
4. 状态机实现为：`IDLE → AWAKE → IN_UTTERANCE → END_CANDIDATE → FINALIZE → THINK → SPEAK → IDLE`。若唤醒后无语音或超时，返回 `IDLE`；若 ASR 空结果，也返回 `IDLE` 并给出“没听清”反馈。
5. 保留按钮：未识别到唤醒词时，按钮仍可直接进入 `LISTEN`；TTS 播放时，按钮作为立即 `interrupt/flush` 的可靠兜底。

**验收标准：** 在目标房间中说出所选唤醒词后，机器人给出可见/可听的 listening 反馈；随后一句中文无需按键即可被提交；普通环境声不上传 ASR；无语音会超时退出；按钮路径仍可工作。

### P2：追问窗口与打断（分开做）

- 回答完成后提供可配置的 5–10 秒 `FOLLOW_UP_LISTEN`，以灯光/表情说明仍可直接说；静默超时回 `IDLE`。Google 的约 8 秒体验可作为产品参照，不是必须照抄的技术阈值。[S3]
- 只有确有“机器人说话时用户可插话”的需求，才单独接入播放参考通道、AEC、半双工/全双工切换和取消传播；不可仅开启 VAD 就宣称支持打断。[S9]

## 必须呈现给用户的反馈

| 状态 | 反馈 | 原因 |
|---|---|---|
| `IDLE` | 常态表情 / 非监听灯 | 避免用户误以为持续录音 |
| `AWAKE/LISTEN` | 短提示音 + 明显听音表情/灯 | 确认唤醒成功、可开始说 |
| `FINALIZE/THINK` | 结束音 + 思考表情 | 说明已停止收音，不必继续说 |
| `SPEAK` | 说话表情/灯 | 表明机器人在回复 |
| 超时、无声、ASR 空结果 | 简短提示后回闲置 | 防止静默失败 |

这不是装饰。Alexa 把 start-of-request 和 endpointing 声音用于让用户确认系统是否正在听，HomePod 也将其状态灯用于 listening / thinking / responding。[S1][S4]

## 风险与决策

1. **不要默认长期开放麦克风。** 这会放大背景噪声、他人说话、隐私和误触发问题；ChatGPT Voice 的官方说明也明确指出这些场景仍会造成 interruption。[S5]
2. **第一版不要承诺 TTS 播放中语音打断。** 当前单 INMP441 与未接 AEC 的路径适合半双工；全双工必须先证明播放参考和实际腔体回声效果。
3. **自定义唤醒词另立项目。** 不让它阻塞“按一次开始 + 自动结束”或“官方唤醒词 + 自动结束”的核心闭环。
4. **阈值是实测决策。** 目标距离、背景噪声、多人说话、音量和腔体尚未被定义；先记录指标再定值。

## 建议的验收指标

- 唤醒：目标距离 / 噪声下的唤醒成功率、每小时误唤醒数。
- 端点：短暂停顿误截断率、用户说完到 `listen.stop` 的延迟、无声误提交率。
- 识别：首字截断率、最终文本有效率、端到端回复首音延迟。
- 打断（仅 P2）：TTS 停止延迟、机器人自身 TTS 被误当成人声的比例。

## 来源

- **[S1]** [Amazon Alexa — Invoking Alexa](https://developer.amazon.com/en-US/docs/alexa/alexa-auto/invoking-alexa.html) — 官方；wake/PTT/TTT、端点检测与状态提示；访问 2026-07-29。
- **[S2]** [Amazon Alexa — Follow Up Mode](https://digprjsurvey.amazon.co.uk/csad/help/node/GX7EJ9WHEPYBV94J) — 官方；免重复唤醒词的追问；访问 2026-07-29。
- **[S3]** [Google Assistant — Continued Conversation](https://support.google.com/assistant/answer/9249169?hl=en) — 官方；约 8 秒追问窗口、结束方式和视觉反馈；访问 2026-07-29。
- **[S4]** [Apple HomePod — Use Siri](https://support.apple.com/guide/homepod/set-up-siri-apd1841a8f81/homepod) — 官方；语音唤醒、长按触控与统一状态灯；访问 2026-07-29。
- **[S5]** [OpenAI Help — ChatGPT Voice](https://help.openai.com/en/articles/20001274) — 官方；会话入口、静音/退出、可打断及其噪声限制；访问 2026-07-29。
- **[S6]** [Espressif ESP-SR — WakeNet](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/README.html) — 官方；ESP32-S3、16 kHz PCM 和 AFE 集成；访问 2026-07-29。
- **[S7]** [Espressif ESP-SR — VADNet](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/vadnet/README.html) — 官方；VAD 参数、延迟与 cache；访问 2026-07-29。
- **[S8]** [Espressif ESP-ADF — Audio Recorder](https://docs.espressif.com/projects/esp-adf/en/latest/api-reference/speech-recognition/audio_recorder.html) — 官方；`vad_off` 端点规则；访问 2026-07-29。
- **[S9]** [Espressif ESP-SR — AEC](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/acoustic_echo_cancellation/README.html) and [Benchmark](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/benchmark/README.html) — 官方；全双工 AEC、VAD/WakeNet 管线和资源基准；访问 2026-07-29。
- **[S10]** [Espressif — Wake Word Customization](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/ESP_Wake_Words_Customization.html) — 官方；自定义词语料、周期和硬件依赖；访问 2026-07-29。
- **[S11]** [Google Cloud Speech-to-Text — Voice activity events](https://docs.cloud.google.com/speech-to-text/docs/voice-activity-events) — 官方；云端开始/结束事件和超时；访问 2026-07-29。
- **[S12]** [Azure Speech — Silence handling](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/how-to-recognize-speech) — 官方；静音阈值的分段/延迟权衡；访问 2026-07-29。
- **[S13]** 当前代码：`firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/recording_button.cpp`、`voice_controller.cpp`、`components/sesame_audio/audio_hal.cpp` — 按键切换和音频上行实现；核查 2026-07-29。
- **[S14]** 当前架构：`docs/architecture/sesame-robot-v3-current-complete-architecture.md` — 目标 WakeNet/VAD 状态机；核查 2026-07-29。
- **[S15]** 当前网关：`endpoint-gateway/src/sesame_endpoint_gateway/app.py`、`endpoint-gateway/README.md` — 仅协议入口，业务 Provider 尚未实现；核查 2026-07-29。
