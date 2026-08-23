# Sesame Robot V3 三条并行推进实施计划

> **执行链路已更新：** 本计划中的 A/B 两线仍可复用；C 线关于“电脑麦克风、电脑扬声器、UART”的部分已被 Wi-Fi/WSS + INMP441/MAX98357 方案替代。当前链路请以 [`../architecture/sesame-robot-v3-wifi-openclaw-architecture.md`](../architecture/sesame-robot-v3-wifi-openclaw-architecture.md) 为准；在该文档的 I2S 引脚可行性确认前，不执行原 C 线实现。

> 日期：2026-07-26  
> 状态：执行计划基线  
> 前提：机器人已组装，但尚未完成 V3 校准、原始功能验收和语音能力开发。  
> 对应系统架构：[UART + 电脑网关架构](../architecture/sesame-robot-v3-uart-gateway-architecture.md)

## 1. 三条线的定义

这不是“硬件完成以后才开始软件”的串行计划，而是三条并行线：

| 线 | 定位 | 是否主要复用现有仓库 | 最终交付 |
| --- | --- | --- | --- |
| A. V3 原始底层线 | 把已经组装的 V3 变成安全、校准正确、可稳定执行原始动作和表情的机器人 | 是。复用电机测试、`movement-sequences.h`、`face-bitmaps.h`、原主固件 | 已校准 V3、舵机编号/subtrim 表、原始动作和表情基线、稳定串口 |
| B. 动作与表情资产线 | 把原有 C++ 动作和位图表情整理成可管理、可校验、可由电脑网关选择的资产 | 部分。内容来自现有仓库，但资产编号、JSON Schema 和校验需要新增 | 动作/表情清单、首批 JSON、离线校验器、资产白名单 |
| C. 新增语音与电脑网关线 | 新增电脑侧语音交互：ASR → 文控网关 → TTS，并把动作/表情意图通过 UART 交给机器人 | 否。这是本项目的新能力 | 可运行电脑网关、ASR/文控/TTS 适配器、UART 控制适配器、完整语音闭环 |

第一版声音的默认边界是：**电脑麦克风采音，电脑扬声器播放 TTS；ESP32 负责动作、表情和说话状态。**当前 V3 基础仓库没有麦克风、Codec、功放和扬声器驱动，所以“机器人本体播放语音”属于后续硬件扩展，不放进本轮 MVP。

## 2. 三条线如何同时启动

| 并行检查点 | A. V3 原始底层线 | B. 动作与表情资产线 | C. 新增语音与电脑网关线 | 允许汇合的条件 |
| --- | --- | --- | --- | --- |
| M0：开始当天 | A1–A2：供电检查、烧录测试固件 | B1：盘点现有动作和表情 | C1–C2：确认电脑音频设备，跑通麦克风和扬声器 | 三条线均可独立开始 |
| M1：基础可见 | A3–A4：8 路映射、90°、Rest/Stand | B2–B3：动作/表情编号表与白名单 | C3–C4：ASR 和 TTS 单独跑通 | 机器人可安全站立；电脑可听和说 |
| M2：可控能力 | A5–A6：原始动作、OLED、串口基线 | B4–B6：动作/表情 JSON 与离线校验 | C5–C6：文控网关契约和模拟电脑网关 | 资产 ID 已冻结，文控结果可被校验 |
| M3：连接机器人 | A7：稳定串口连接 | B7：首批动作/表情验收 | C7–C8：UART JSON 和 Robot Adapter | 电脑能按 ID 驱动真实动作/表情并收到回执 |
| M4：语音闭环 | 支持 `stop` 和状态回传 | 维护资产版本与白名单 | C9–C10：对话、动作、表情、打断和稳定性 | 完成端到端验收 |

## 3. A 线：V3 原始底层与硬件基线

这条线不开发 AI，也不改动作设计；目标是确认你刚组装的机器人本身可靠。

| 步骤 | 具体动作 | 使用的现有文件/技术 | 输出物 | 完成标准 |
| --- | --- | --- | --- | --- |
| A1. 供电和线束安全检查 | 断电检查电源正负、5 V 舵机电源、3.3 V 逻辑电源、公共地、舵机插头方向、OLED 线和裸露导线；舵机臂不要先锁死。 | 万用表、V3 Wiring Guide、可靠 USB-C 电源或限流电源 | 供电检查记录 | 无短路；5 V/3.3 V 实测正确；无异常发热；具备快速断电方式。 |
| A2. V3 电机测试固件 | 修改 `sesame-motor-tester.ino`，启用 V3 舵机 GPIO：`4/5/6/7/10/11/12/13`；烧录到 ESP32-S3。 | Arduino IDE 2.x、Arduino-ESP32、ESP32Servo 3.0.9、`debugging-firmware/sesame-motor-tester.ino` | 可烧录的 V3 电机测试固件 | 115200 串口显示测试菜单；`stop` 可以释放全部舵机。 |
| A3. 8 路舵机编号映射 | 逐个发送 `0,90` 到 `7,90`；记录实际关节是否分别对应 R1、R2、L1、L2、R4、R3、L3、L4；检查插头棕线方向。 | 串口监视器、机械角度图、测试固件 | `servo-mapping.md` 或表格 | 每个编号只控制一个正确关节；没有接错、反向、卡死或异常抖动。 |
| A4. 机械零位和 subtrim | 所有舵机在 90° 时安装舵机臂；烧录原始主固件，执行 `rest`/`stand`；记录每路偏差。 | `movement-sequences.h`、`servoSubtrim[8]` | subtrim 表和照片 | Rest/Stand 无碰撞、无持续堵转，机器人可稳定站立。 |
| A5. 原始 V3 主固件 | 将 `sesame-firmware-main.ino` 切换为 V3 舵机 GPIO 和 OLED I2C GPIO `8/9`；不要保留当前 S2 Mini 活跃配置。 | 原始主固件、Adafruit SSD1306、ESP32Servo | 已验证的 V3 基线固件 | 开机不乱动；OLED 正常显示；串口无持续错误。 |
| A6. 原始能力冒烟测试 | 重复执行 `rest`、`stand`、`wave`、`stop`；重复显示 `idle`、`thinking`、`talk_happy`。 | 现有动作函数和表情位图 | 原始功能验收表 | 四个动作各连续 20 次无重启；OLED 无 I2C 错误；`stop` 在动作中有效。 |
| A7. 串口连接基线 | 确认使用 USB CDC 或独立 UART1；记录电脑识别到的端口、波特率、重连行为。独立 UART1 未确认 V3 原理图前，不指定 RX/TX GPIO。 | V3 PCB 原理图、USB-C 数据线、串口工具 | 串口连接记录 | 电脑可以稳定打开唯一端口，插拔后可重新识别；调试日志可见。 |

**A 线完成定义：** 已有一台能安全站立、执行原始动作、显示原始表情、稳定连到电脑的 V3。此时它仍不是语音机器人，但已经是可供另外两条线接入的执行器。

## 4. B 线：动作与表情资产

这条线不需要等语音完成。它把现有动作和表情从“散落在 C++ 函数和位图数组中”整理为电脑网关可安全选择的资产。

| 步骤 | 具体动作 | 使用的现有文件/技术 | 输出物 | 完成标准 |
| --- | --- | --- | --- | --- |
| B1. 盘点原始动作 | 从 `movement-sequences.h` 列出全部可调用动作：`rest`、`stand`、`wave`、`dance`、`walk` 等；标记是否循环、预计时长、是否可中断、是否需要先站立。 | `movement-sequences.h` | `action-catalog.json` 初稿 | 每个现有动作都有唯一 ID、中文名称、风险说明和可中断说明。 |
| B2. 冻结首批动作白名单 | MVP 只保留低风险动作，例如 `rest`、`stand`、`wave`、`point`；将走路、跳舞或高摆幅动作列为后续。 | 机械验收结果、动作目录 | `allowed-actions.v1.json` | 文控网关只能看到白名单 ID；高风险动作没有进入第一版。 |
| B3. 盘点原始表情 | 从 `FACE_LIST` 列出表情，区分静态、循环、说话表情和动作伴随表情。 | `face-bitmaps.h`、`faceFpsEntries` | `expression-catalog.json` 初稿 | `idle`、`thinking`、`happy`、`talk_happy` 等首批表情都有唯一 ID、模式和 FPS。 |
| B4. 定义动作 JSON | 固定 `sesame.action.v1`：ID、版本、风险级别、默认表情、循环次数、帧数据或预置动作引用。第一版用“预置动作引用”，不在运行时传整段舵机帧。 | JSON Schema、Pydantic、B1/B2 | `action.schema.json` 和首批样例 | 缺 ID、未知动作、非法循环次数、越界角度均无法通过校验。 |
| B5. 定义表情 JSON | 固定 `sesame.expression.v1`：ID、版本、128×64、FPS、模式和帧引用。 | JSON Schema、Pydantic、B3 | `expression.schema.json` 和首批样例 | 只允许 `once/loop/boomerang`；不存在的帧和超范围 FPS 被拒绝。 |
| B6. 补齐创作转换流程 | Sesame Studio 目前只导出 `setServoAngle()` C++；先手工把一个动作转换为 JSON 模板，后续再写转换器。图片通过 image2cpp 转为 1 位位图，继续编译进固件。 | Sesame Studio、image2cpp、Pixelorama（可选） | `studio-to-action-json` 规则、一个转换样例 | 同一动作在文档 JSON 与原始 C++ 中的舵机序列可对照。 |
| B7. 离线资产校验 | 实现电脑侧校验：Schema、白名单、动作 ID、表达 ID、机械角度/时长约束。 | Python、Pydantic、pytest | `asset-validator` 和测试样例 | 正确样例通过；错误版本、未知 ID、缺字段、非法角度全部失败。 |
| B8. 真实机器人验收 | 用原始固件按首批动作/表情 ID 逐一执行；记录是否需要降低速度、缩短时长或从白名单移除。 | A 线完成的 V3、验收表 | 资产验收记录与 `allowed-actions.v1.json` | 每个进入白名单的动作都有真实机器人通过记录。 |

**B 线完成定义：** 电脑网关可以只根据 `action_id` 和 `expression_id` 做出安全、确定的机器人表达选择，不需要让大模型直接写舵机角度。

## 5. C 线：新增语音与电脑网关

这条线是新增功能。C1–C6 可以在 A/B 线进行时独立开发和测试；只有 C7 之后才需要真实机器人。

| 步骤 | 具体动作 | 技术/工具 | 输出物 | 完成标准 |
| --- | --- | --- | --- | --- |
| C1. 冻结第一版音频边界 | 明确 MVP 使用电脑麦克风采音、电脑扬声器播放。记录采样率、单声道、录音设备名和播放设备名。 | macOS 音频设置、sounddevice 或 PyAudio | `audio-config.toml` 或 `.env` 样例 | 程序能列出并选择正确输入/输出设备。 |
| C2. 单独验证电脑音频 | 写最小录音和播放程序，不接 ASR；录 5 秒、保存 WAV、立即回放。 | Python 3.12、sounddevice/PyAudio、WAV | `audio-smoke-test.py` | 连续 10 次录音/播放无设备占用、无明显音频截断。 |
| C3. 接入 ASR | 抽象 `AsrProvider`；先选一个本地或云 ASR，接入 VAD；仅把 `final` 文本交给下一步。 | FunASR/Whisper/云 ASR、VAD、asyncio | `asr/` 模块与测试录音 | 20 条短句有可观察结果；无声、超时、识别失败都有错误状态。 |
| C4. 接入 TTS | 抽象 `TtsProvider`；输入文本输出 WAV/PCM；支持取消当前播放。 | 本地或云 TTS、sounddevice/PyAudio | `tts/` 模块和测试文本 | 固定中文文本能播放；取消后不继续播放旧音频。 |
| C5. 固定文控网关契约 | 确认文控网关是 OpenClaw、其他 LLM 网关还是自定义服务；固定认证方式、HTTP/WebSocket、超时、输入和输出 JSON。 | HTTPX/WebSocket、Pydantic | `text_gateway/contract.md`、请求/响应模型 | 输入含文字、会话 ID、允许资产列表；输出必须有 `reply_text`，动作/表情可选。 |
| C6. 建立电脑网关状态机 | 实现 `idle → listening → recognizing → thinking → speaking → idle`；先接模拟 ASR、模拟文控、模拟 TTS。 | asyncio、Pydantic、pytest | `computer_gateway/` 骨架 | 固定文字可产生模拟回复和状态转换；异常不会卡在中间状态。 |
| C7. 新增 UART JSON 协议 | 将 ESP32 原有 32 字节 CLI 改为 NDJSON：`system.hello`、`motion.play`、`expression.set`、`speech.state`、`robot.stop`、`ack`、`error`。调试日志不能混进业务流。 | ArduinoJson、C++ 环形缓冲、pyserial-asyncio | ESP32 UART 模块、电脑串口管理器 | 半包、粘包、超长 JSON、非法 JSON 都安全处理；每条命令有回执。 |
| C8. 接入 Robot Adapter | 电脑网关把文控结果中的 `action_id`、`expression_id`、`speech_state` 转换成 UART 命令，等待 ESP32 回执。 | C7、B 线资产目录 | `robot_adapter.py` | 电脑可按 ID 触发真实动作/表情；未知 ID 在电脑端被拒绝。 |
| C9. 汇合完整对话 | 连通“电脑麦克风 → ASR → 文控网关 → TTS → 电脑扬声器”与“UART → 动作/表情”。TTS 开始与结束时同步 `speech.state`。 | A/B/C 已交付模块 | 首个端到端演示 | 一句语音能得到回复、播出语音、执行允许动作、显示表情和收到回执。 |
| C10. 打断和稳定性 | 用户新说话时取消旧文控请求和 TTS，发送 `robot.stop` 或恢复表情；测试 UART 断开、文控超时、TTS 失败、1 小时运行。 | asyncio 取消、超时、日志、pytest | 故障测试记录 | 不播放旧语音；动作可停止；错误有记录且可恢复。 |

**C 线完成定义：** 用户对电脑说话后，机器人能通过 ASR、文控网关和 TTS 完成回应，并用已经验收的动作与表情做同步表达。

## 6. 三线最终汇合验收

只有以下场景全部通过，才认为第一版语音机器人完成：

1. A 线：机器人上电、站立、停止和 OLED 表情均稳定。
2. B 线：`wave`、`stand`、`rest`、`talk_happy` 等首批资产有明确 ID、版本和真实验收记录。
3. C 线：用户说“跟我打个招呼”，系统返回文字并播放 TTS；电脑网关同时下发 `wave` 与 `talk_happy`。
4. 用户在 TTS 播放中打断，旧语音停止，表情退出说话状态，动作不会失控。
5. 文控网关或 UART 断开时，系统记录错误并让机器人进入安全状态。

## 7. 不要提前做的事

- 不要先做机器人本体麦克风/扬声器实时音频传输；当前 V3 基础代码没有这条硬件和驱动链。
- 不要让文控网关或大模型直接下发 8 路舵机角度。
- 不要把 WAV、PCM 或 OLED 位图 Base64 塞进运行期 JSON。
- 不要在没有 A3/A4 记录前执行高摆幅动作、行走或跳舞。
- 不要在没有 C5 契约前接入真实文控网关。
