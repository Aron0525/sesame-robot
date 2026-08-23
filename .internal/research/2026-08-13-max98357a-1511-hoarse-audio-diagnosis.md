# Research: MAX98357A + 1511 腔体喇叭声音嘶哑排查

> **Date:** 2026-08-13
> **Bead:** 未创建（当前环境没有 `bd` 命令）
> **Status:** Complete

## Summary

ADI/Maxim 可核验型号是 MAX98357A；没有找到 MAX98572A 的官方产品资料。按 MAX98357A 判断，当前项目的 16 kHz、32-bit stereo、Philips I2S 格式与芯片兼容，最高概率问题变成输出增益/PCM 幅度过大、1511 喇叭过载、5 V 供电瞬时掉压、SD_MODE/接线问题或上游 PCM/Opus 数据失真，而不是功放接口类型错误。[S1][S2]

## Key Findings

### 1. 1511 通常是 8 Ω、约 0.7–1 W，不是 8 W

> **Confidence:** high — 15 mm × 11 mm 厂家规格一致。

Soberton 的 1511 型号为 8 Ω、额定/最大 0.7/1 W。[S3] 8 Ω、1 W 对应约 2.83 Vrms；0.7 W 对应约 2.37 Vrms。MAX98357A 在 5 V、8 Ω 下可输出 1.4 W（1% THD+N）或 1.8 W（10% THD+N），足以让 1 W 的 1511 喇叭过载并产生沙哑、拍边或永久损伤。[S1]

### 2. 当前项目 I2S 格式与 MAX98357A 原则上匹配

> **Confidence:** high — 项目代码与官方支持格式直接对应。

MAX98357A 支持标准 I2S、8–96 kHz 采样率以及 16/24/32-bit 数据。[S1] 当前固件使用 16 kHz、32-bit stereo、Philips I2S，并把 mono PCM 复制到左右 slot，因此采样率、格式和位宽本身都在支持范围内。[S2] 但 BCLK、LRCLK、DIN 任一接错、虚焊或信号完整性差，仍会产生持续沙哑或数字杂音。

### 3. GAIN_SLOT 默认悬空为 9 dB，可能过大

> **Confidence:** high — 官方增益表给出五档硬件配置。

MAX98357A 的 GAIN_SLOT 悬空是 9 dB；接 VDD 是 6 dB；经 100 kΩ 接 VDD 是 3 dB；接 GND 是 12 dB；经 100 kΩ 接 GND 是 15 dB。[S1] 当前固件没有数字音量缩放，直接把解码后的 PCM16 左移后送入 I2S。[S2] 如果模块 GAIN 悬空或接地、而 TTS PCM 接近 0 dBFS，就容易撞到电源输出摆幅并发生削顶；同时也可能超过 1511 的约 1 W 额定功率。

### 4. 供电与退耦不足会在音频峰值时失真

> **Confidence:** high — 官方应用电路明确要求 0.1 µF + 10 µF，并建议长电源线增加储能电容。

MAX98357A 的 VDD 范围是 2.5–5.5 V。官方要求芯片附近至少有 0.1 µF 和 10 µF 旁路电容；电源走线长时增加 bulk capacitance（大容量储能电容）。[S1] 如果 5 V 与舵机共用、线细或电源限流，音频峰值时 VDD 下跌会造成削顶、爆音或断续。

### 5. SD_MODE 和 BTL 输出必须正确

> **Confidence:** high — 官方引脚和模式表明确。

SD_MODE 为低时芯片关断；直接拉高选择左声道，通过不同电阻上拉可选择右声道或左右混合。[S1] 当前 GPIO1 只在 TTS 播放期间拉高。喇叭必须接在 OUTP 与 OUTN 之间，任一输出端都不能接 GND。若输出短路或负载异常，2.8 A 典型限流会让输出每约 100 µs 关断再重试，听感可能类似爆裂或信号不稳。[S1]

### 6. 本地测试音可以把功率级与网络音频链分开

> **Confidence:** high — 直接的分段诊断方法。

如果本地 -20 dBFS、1 kHz PCM 清楚，而联网 TTS 嘶哑，应查 Opus 包、解码幅度、丢包/乱序和播放缓冲；如果本地测试音也嘶哑，则查增益、喇叭、VDD、I2S 接线和功放板。

## Codebase Context

- `audio_hal.cpp` 把 TX 配成 16 kHz、32-bit stereo、Philips I2S，符合 MAX98357A 支持范围。
- 每个 PCM16 样本左移 16 位并复制到左右 slot；代码没有播放增益衰减或 limiter（限幅器）。
- GPIO1 控制 SD_MODE：开始 TTS 时拉高，结束或 flush 时拉低。
- GPIO14/47/2 分别是 BCLK/LRCLK/DIN。

## Recommendations

1. 再核对型号：如果实物是常见模块，芯片应为 `MAX98357A`；若确实印 `MAX98572A`，拍清晰近照后重新确认。
2. 确认 1511 完整规格。若是 8 Ω、1 W，把输出限制在约 2.8 Vrms 以内。
3. 将 GAIN_SLOT 改为 3 dB（100 kΩ 接 VDD）或 6 dB（直接接 VDD），同时先把软件 PCM 衰减 12–18 dB。
4. 脱离网络播放本地 1 kHz、-20 dBFS 正弦和一段已知干净的语音 PCM。
5. 换一只已知完好的 8 Ω 喇叭；如果立刻恢复，原 1511 已过载或结构共振。
6. 用示波器在芯片 VDD 脚观察破音瞬间，检查是否从 5 V 明显下跌；芯片旁放 0.1 µF + 10 µF，供电线较长时加 100–470 µF 作为诊断用储能。
7. 核对 BCLK=GPIO14、LRCLK=GPIO47、DIN=GPIO2、SD_MODE=GPIO1 和公共 GND；喇叭只跨接 OUTP/OUTN。
8. 若本地音频正常而在线 TTS 异常，停止更换功放，转查 Opus/PCM 和播放缓冲。

## Open Questions

- 实际芯片丝印究竟是 MAX98357A 还是 MAX98572A？
- MAX98357A 的 VDD 是 3.3 V 还是 5 V，是否和舵机共用？
- GAIN_SLOT 当前是悬空、接 VDD 还是接 GND？
- 低音量是否清楚，还是所有音量都嘶哑？

## Refuted / Discarded Claims

- “MAX98357A 是模拟功放，可能与 I2S 不兼容”：错误。MAX98357A 本身就是标准 I2S 数字输入 D 类功放。[S1]
- “当前 16 kHz/32-bit 配置不受支持”：错误。该芯片支持 16 kHz 和 32-bit I2S。[S1]
- “1511 是 8 W 喇叭”：常见 1511 规格不支持这一判断，通常是 8 Ω、约 0.7–1 W。[S3]

## Sources

- [ADI MAX98357A product page](https://www.analog.com/en/products/max98357a.html) — Primary/Official — accessed 2026-08-13 — 产品型号、I2S、采样率和输出能力。
- [ADI MAX98357A/MAX98357B data sheet Rev.16](https://www.analog.com/media/en/technical-documentation/data-sheets/max98357a-max98357b.pdf) — Primary/Official — accessed 2026-08-13 — [S1] 电气指标、GAIN_SLOT、SD_MODE、限流、退耦和接线。
- [Sesame Robot V3 audio HAL](../../firmware-work/Sesame_Robot_V3_IDF/components/sesame_audio/audio_hal.cpp) — Project source — accessed 2026-08-13 — [S2] 当前 I2S 配置、PCM 写入和 SD_MODE 控制。
- [Soberton SP-1511-3 product page](https://www.soberton.com/sp-1511-3/) — Primary/Official — accessed 2026-08-13 — [S3] 1511 尺寸、阻抗和额定功率。
