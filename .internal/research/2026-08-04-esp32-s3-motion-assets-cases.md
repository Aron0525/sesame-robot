# ESP32-S3 专项：动作设计、下发、保存与本地播放案例

> 日期：2026-08-04  
> 范围：只计入官方资料或源码明确标注 ESP32-S3 的案例。普通 ESP32、ESP32-WROOM、ESP32-U4WDH 不视为同一芯片案例。

## 结论

扩展核查 GitHub 源码、厂商文档、Reddit/Arduino 类论坛、App Store 和 Google Play 后，仍没有找到一项公开案例完整证明以下五点同时成立：

1. 明确使用 ESP32-S3；
2. 上位机或 LLM 生成新的多舵机动作；
3. 通过 Wi-Fi/BLE 把动作作为 JSON/独立资产下发；
4. ESP32-S3 持久保存多个动作；
5. 不更新固件即可在重启后按 ID/名称播放。

最接近的是 Otto/闪猫侠：它已实现“语言/AI → MCP → JSON 舵机序列 → ESP32-S3 运行时排队执行”，但当前源码只把序列放入 FreeRTOS 队列，没有动作资产持久化。Seeed Atom-S/Atom-X 证明 ESP32-S3 上的网页动作编辑、JSON 动作和本地播放，但官方离线动作流程仍要求重新编译烧录。Bottango + ESP32-S3 的实作证明电脑设计动画、S3 存储并独立播放，但公开流程是导出后编译进固件或复制到 SD 卡，不是无线动作资产发布。

新增发现的 CYOBot v2 是目前最强的“无线部署并持久运行”证据：官方 ESP32-S3 四足机器人允许浏览器通过 Wi-Fi 提交 MicroPython 程序，固件把程序写到 microSD，重启后自动替换并运行 `main.py`。这证明“不接 USB、不重新烧录底层固件，也能增加机器人行为”。但它下发的是 Python 程序，不是受约束的 JSON 动作资产；公开资料也没有证明由 LLM 自动生成舵机动作。因此它仍不是 Sesame 严格条件下的同类完成案例。

**成熟度结论：严格匹配数为 0。** 按“必须有同芯片、同链路的已完成项目”这一选型标准，当前不应把“LLM 自动创造动作并作为 JSON 资产持久发布到 ESP32-S3”列为已成熟的主方案。可以把它列为研发实验项；成熟的子能力是独立存在的，但尚未在一个公开产品中闭环。

## 案例 1：Otto/闪猫侠 AI 桌面机器人

**匹配程度：3/4，最接近 OpenClaw 方案。**

- 官方项目固件的板级配置明确将 `otto-robot` 目标设为 `esp32s3`。
- 用户语音经后台大模型后，通过 MCP 调用 `self.otto.servo_sequences`。
- 动作载荷是 JSON，包含各舵机的绝对角度、速度、延迟，或振荡器的振幅、中心角、相位、周期和次数。
- ESP32-S3 接收 JSON 后放入动作队列，由通用执行器顺序播放；新增即兴动作不需要重新编译固件。
- 当前实现的 JSON 缓冲区只有 512 字节；复杂动作采用多次 MCP 调用、分段排队。
- 源码只显示复制到 RAM 中的 FreeRTOS 队列；NVS 只用于舵机 trim。公开实现没有 `save_action`、动作注册表或重启后按名称恢复，因此不属于完整的“持久动作资产库”。

链路：

```text
语音 → 小智/LLM → MCP servo_sequences(JSON)
→ ESP32-S3 FreeRTOS 动作队列 → 通用舵机序列播放器 → 6 路舵机
```

## 案例 2：Seeed Studio Atom-S / Atom-X

**匹配程度：2/4，适合作为动作编辑器和数据格式参考。**

- Atom-S 是 10 自由度、Atom-X 是 17 自由度人形机器人，主控均为 XIAO ESP32-S3。
- 官方 Web Motion Editor 支持读取舵机状态、拖动角度、示教记录关键帧、调整时间/间隔、实时回放和导出 JSON。
- 机器人在运行时也能无线示教并回放当前录制序列，但文档明确该示教数据位于内存，HOME 会清除。
- 要让导出的 JSON 成为掉电保留的离线动作，官方流程是替换 `Robot.ino` 中的 `jsonData`，重新编译并烧录 ESP32-S3。

因此 Atom-S/Atom-X 证明“动作可以是 JSON 数据”，同时也说明：如果固件没有预先实现文件系统、上传接口和资产注册表，动作数据仍会被当作固件常量重新烧录。Sesame 要避免的正是这一限制。

## 案例 3：Bottango + Waveshare ESP32-S3-Zero 动画机器人

**匹配程度：2.5/4，属于明确的 ESP32-S3 实现案例，不是量产机器人产品。**

- Bottango 官方固件支持 Arduino 兼容控制器，官方文档把 ESP32 作为高容量离线动画控制器，并允许导出 firmware code、SD card files 或 JSON。
- 公开的 Droid-style animatronic 实作明确使用 Waveshare ESP32-S3-Zero、9 个舵机和 PCA9685；S3 保存动画并在没有电脑时独立播放。
- 它验证了“PC 负责关键帧设计，ESP32-S3 负责存储和播放”的硬件可行性。
- 但公开流程主要是把导出的动画编译进程序，或经 SD 卡文件部署；没有证明云端通过 Wi-Fi 新增多动作资产。

作者在教程中进一步明确说明：为了省去 SD 卡读卡器，他使用 Copilot/Claude 4 把动画硬编码进 MCU 内存；当前形态不能直接连接 Bottango 继续新增动画。这一源码/教程事实排除了把它算作“无线动作资产发布”。

## 案例 4：CYOBot v2

**匹配程度：4/5 的相邻实现；数据形态不匹配。**

- 官方仓库明确为 ESP32-S3、8 MB PSRAM 的可变形四足机器人，并提供 Python/Block/C++ 编程入口、舵机运动学库和 ChatGPT 扩展方向。
- 官方 `main.py` 的 `/api/deploy` 接口接收 JSON 请求中的 `code` 字段，把内容写入 `/sdcard/main.py` 后重启。
- 官方 `boot.py` 在启动时检测 `/sdcard/main.py`，复制成板载 `main.py`、删除暂存文件，然后由 MicroPython 运行；行为可跨重启保留，不需要再次烧录 MicroPython 固件。
- 它是“固定解释器固件 + 无线下发新行为 + 持久化 + 重启执行”的真实机器人产品案例。
- 差异是：新增内容为任意 Python 程序，不是由通用播放器解释的 JSON 关键帧动作；也没有找到 LLM 自动把自然语言变成舵机动作并部署的完成链路。

链路：

```text
浏览器 Python/Block Portal → Wi-Fi POST /api/deploy
→ microSD /main.py → ESP32-S3 重启 → boot.py 安装 → MicroPython 执行舵机程序
```

## 案例 5：TNY-360 + TNY-Coder

**匹配程度：3/5；动作程序在电脑端执行。**

- 官方仓库明确使用 ESP32-S3 N16R8、12 个腿部舵机，Core 0 执行 200 Hz 控制环，Core 1 处理 WebSocket/UI。
- TNY-Coder 是跨平台 Blockly 桌面 App，可控制单舵机、脚端位置、身体姿态并组合复杂动作，通过 WebSocket 实时连接实体机器人。
- 源码显示 Blockly 程序在桌面 App 的 JavaScript 运行循环中执行，再逐条发送关节/姿态命令；“保存”会下载本机 `.tnycode` 文件。
- TNY-360 固件虽划有 LittleFS 存储分区，但公开源码未发现动作文件上传、动作资产解析或重启后本地播放路径。

因此它证明“ESP32-S3 + 上位机图形化动作设计 + 无线实时执行”，不证明“动作资产发布后脱离电脑运行”。

## 案例 6：RobotMotion ESP-IDF 组件

**匹配程度：3/5；是组件，不是完成机器人产品。**

- Espressif Component Registry 上的 `ningzixi/robot_motion` 明确标注 ESP32/ESP32-S3，支持多舵机平滑动作、动作序列和 JSON 动作解析。
- README 明确提出可通过 Function Calling 与 Coze 等大模型平台集成，并给出 `motion_exec_json()`。
- 源码只负责解析传入字符串并执行；仓库没有网络接收、LittleFS/NVS 动作保存、动作索引或重启恢复实现，且 Registry 显示 0 个示例、0 个依赖项目。

它是最贴近 Sesame 数据结构的播放器组件证据，但不能作为端到端产品成熟度证据。

## 案例 7：SpotMicroESP32-Leika

**匹配程度：2.5/5；文件上传与动作系统没有接通。**

- 仓库推荐 ESP32-S3 N8R8，固件明确含 ESP32-S3 构建目标、LittleFS、浏览器 UI 和 WebSocket 文件分块上传。
- 文件系统协议与 UI 能把任意文件写入 ESP32-S3 的 LittleFS。
- 运动系统只有 REST/STAND/WALK 等 C++ 状态和实时角度/步态命令；源码未发现从上传文件加载关键帧/动作并播放的路径。

因此不能把“有文件上传”与“有运动控制”拼接推断成已经实现动作资产播放器。

## App 与论坛核查

| 候选 | 已验证内容 | 为什么不计为严格匹配 |
|---|---|---|
| Q1 Robot Motion Editor（App Store） | BLE 编辑关键帧、保存 JSON、上传到 ESP32 Slot、脱离 iPad 执行 | App 页面未给出 ESP32-S3；Q1 lite 官方页面只写 TinyPlan97 控制板 |
| ESPGeeker Home（Google Play） | 明确 ESP32-S3，App 经 Wi-Fi 上传和管理视频资产，设备本地播放 | 证明表情/媒体资产链路，不是多舵机机器人动作 |
| TNY-360 Reddit 展示 | 实机运行 ESP32-S3、开源 GitHub、Web UI 和动作演示 | 与源码一致：当前没有动作资产持久发布 |
| ESP32-S3 Bottango 教程/论坛传播 | 真实九舵机成品、电脑关键帧设计、独立播放 | 教程明确硬编码或 SD 卡部署，新增动画仍要更新代码/介质 |
| Claude/ESPBridge 桌面机器人 | LLM/MCP 通过蓝牙实时控制表情、引脚和可选舵机 | 未明确 ESP32-S3；动作是在线工具调用，不是设备动作资产 |

## 排除项

| 案例 | 排除原因 |
|---|---|
| Petoi Bittle X / BiBoard V1 | 官方规格为 ESP32-U4WDH，不是 ESP32-S3 |
| Hiwonder miniHexa | 官方规格为 ESP32-WROOM，不是 ESP32-S3 |
| Bottango Solar | 官方资料只明确 ESP32，未核实具体为 ESP32-S3 |
| WAVEGO Pro | 官方只标注 ESP32，未核实为 ESP32-S3 |
| Q1 Robot Motion Editor | App 确认能把 JSON 上传到 ESP32 存储槽并离线播放，但没有找到控制器为 ESP32-S3 的官方证据 |

## 对 Sesame 的判断

这条路线的技术风险不在 ESP32-S3 性能，而在产品软件缺少的三层：

1. **动作资产协议**：`MotionClip` 的版本、舵机 ID、关键帧、时间、插值、CRC/hash。
2. **持久化资产库**：LittleFS/FAT 分区、多槽注册表、原子写入、空间配额、删除与回滚。
3. **发布接口**：`action.upload`、`action.validate`、`action.commit`、`action.list`、`action.play`。

Otto 已证明 LLM 可以依据舵机语义生成 JSON 序列并由 ESP32-S3 运行时执行；Sesame 需要在它的“队列执行”之后补上“校验—持久保存—按 ID 播放”。这属于固件播放器能力的扩展，不要求每增加一个动作就重新烧录固件。首次加入播放器和资产分区时仍需要一次固件升级。

## 来源

- [Otto DIY 官方使用说明](https://ottodiy.tech/docs/usage/) — ESP32-S3 AI 机器人、MCP 舵机序列自编程、JSON 参数与动作队列。
- [Otto ESP32-S3 板级配置](https://github.com/txp666/xiaozhi-esp32/blob/93b185d0e0fae8ae87db040f9e090bc561b8e2fd/main/boards/otto-robot/config.json#L1-L12) — `target: esp32s3`。
- [Otto 舵机序列实现](https://github.com/txp666/xiaozhi-esp32/blob/93b185d0e0fae8ae87db040f9e090bc561b8e2fd/main/boards/otto-robot/otto_controller.cc#L453-L481) — JSON 复制到 RAM 队列；未见动作资产持久化。
- [Seeed Atom-S 官方文档](https://wiki.seeedstudio.com/atom_s/) — XIAO ESP32-S3、Web Motion Editor、JSON 导出、运行时示教和重新烧录流程。
- [Seeed Atom-X 官方文档](https://wiki.seeedstudio.com/atom_x/) — XIAO ESP32-S3、17 自由度、网页动作编辑器。
- [Bottango Microcontroller Choice](https://docs.bottango.com/learn-bottango/faqs/microcontroller-choice/) — ESP32 离线动画存储和连接能力。
- [Bottango Final Export](https://docs.bottango.com/learn-bottango/exporting-animations/final-export/) — firmware code、SD card files、JSON 三种动画数据输出。
- [ESP32-S3 Bottango 动画机器人实作](https://www.instructables.com/Droid-Style-RC-Animatronics-Using-Bottango-and-ESP/) — Waveshare ESP32-S3-Zero 保存动画并独立播放。
- [CYOBot v2 官方仓库](https://github.com/CYOBot/CYOBot-v2/tree/012872bc0f153ee3973f0e3a504448db3dc90fdb) — ESP32-S3、Wi-Fi coding portal、舵机运动学与 MicroPython 系统。
- [CYOBot `/api/deploy` 源码](https://github.com/CYOBot/CYOBot-v2/blob/012872bc0f153ee3973f0e3a504448db3dc90fdb/software/MicroPython/pyboard/main.py#L290-L296) — 把无线提交的代码写入 `/sdcard/main.py` 并重启。
- [CYOBot 启动安装源码](https://github.com/CYOBot/CYOBot-v2/blob/012872bc0f153ee3973f0e3a504448db3dc90fdb/software/MicroPython/pyboard/boot.py#L7-L25) — 重启后把 SD 卡程序安装为 `main.py`。
- [CYOBot 官方下载页](https://www.cyobot.com/downloads) — Python Portal 和 Block Portal。
- [TNY-360 官方仓库](https://github.com/TNY-Robotics/TNY-360/tree/42bbc828656d3ef79a6c92a504544d601bce0b4f) — ESP32-S3 N16R8、200 Hz 控制环、WebSocket/UI。
- [TNY-Coder 官方仓库](https://github.com/TNY-Robotics/TNY-Coder/tree/62b44a293693caba555b7b7c02cdb8d1600ee72d) — Blockly 桌面 App 和 WebSocket 实时控制。
- [TNY-Coder 本地保存源码](https://github.com/TNY-Robotics/TNY-Coder/blob/62b44a293693caba555b7b7c02cdb8d1600ee72d/app/components/AppHeader.vue#L67-L104) — 工程下载为电脑本地 `.tnycode`。
- [ESP32 RobotMotion 组件](https://components.espressif.com/components/ningzixi/robot_motion/versions/1.0.0) — ESP32-S3、JSON 动作序列、LLM Function Calling；没有持久化实现。
- [SpotMicroESP32-Leika](https://github.com/runeharlyk/SpotMicroESP32-Leika/tree/26c9d5b6853eefaf51aafbae19d202bf68113f66) — ESP32-S3、LittleFS 文件上传和实时运动控制，但未接成动作文件播放器。
- [Q1 Robot Motion Editor（App Store）](https://apps.apple.com/ca/app/q1-robot-motion-editor/id6761700491) — ESP32 Slot/JSON 离线动作链路；芯片型号未证明为 S3。
- [ESPGeeker Home（Google Play）](https://play.google.com/store/apps/details?id=com.espgeeker.homeapp) — ESP32-S3 媒体资产经 Wi-Fi 上传和本地播放。
