# 调研：上位机设计动作、机器人保存动作数据并本地播放的产品案例

> 范围更正（2026-08-04）：本文最初按“ESP32 系列或同类机器人架构”调研，**不能作为 ESP32-S3 芯片级案例清单**。Petoi BiBoard V1 是 ESP32-U4WDH，miniHexa 是 ESP32-WROOM；Bottango Solar 官方资料只明确到 ESP32。ESP32-S3 专项结论见 `/Users/mac/Documents/sesame robot/.internal/research/2026-08-04-esp32-s3-motion-assets-cases.md`。

> 日期：2026-08-04  
> 状态：完成  
> Bead：本机未安装 `bd`，跳过  
> 问题：是否已有真实产品采用“电脑端设计动作，控制器保存动作数据，固件通用播放器按编号/名称执行；新增动作不重刷整个固件”的模式？

## 摘要

这种模式不仅可行，而且已经是教育机器人、舵机控制器和高端四足机器人中的成熟架构。最贴近 Sesame 的案例是 Petoi Bittle X（ESP32 + Wi-Fi/WebSocket + NVS 临时技能）和 Bottango Solar（ESP32 + microSD 动画资产 + 本地播放器）；ROBOTIS、Hiwonder 和 Boston Dynamics Spot 又从不同产品层级证明了“程序逻辑与动作数据分离、上传动作、按 ID/名称本地执行”是稳定做法。

目前没有发现主流产品完整公开了“语音 → LLM 自动设计动作 → 云端发布多动作资产 → ESP32 持久化播放”整条链路。Alter3 研究已证明 LLM 能将自然语言转成多关节动作；Sesame 的工程价值是把这项能力与成熟的动作资产管线连接起来。

## 关键发现

### 1. Petoi Bittle X / BiBoard：最接近 Sesame 的 ESP32 + Wi-Fi 案例

> 置信度：高——Petoi 官方文档、固定版本官方源码与本地镜像相互印证。

Petoi 的 Desktop App / Skill Composer 用关节角度和动作帧设计 Bittle/Nybble 技能。动作是 `K` token 加数字数组，而不是为每个动作增加新的固件函数。[S1][S2]

当前 OpenCatESP32 源码能够通过 USB、Bluetooth，或 Web Coding Blocks 的局域网 WebSocket 收到完整技能数组；ESP32 默认把最后一个动态技能写入 Preferences/NVS 的 `tmp` 键，然后由通用 `Skill` 类解析帧数据并本地播放。官方说明这个技能掉电重启后仍可调用，无需再次编译或上传固件。[S1][S2][S3]

限制是设备端只有一个动态 `tmp` 槽：下一个技能会覆盖上一个。Petoi 将任意数量的新技能纳入内置命名列表时，官方流程仍会修改 `Instinct***.h` 并上传固件。因此 Petoi 证明了完整技术链路，但 Sesame 仍需把单槽扩展为多资产注册表。[S1][S4]

对应链路：

```text
Skill Composer / Web Coding Blocks
→ 技能帧数组
→ USB / Bluetooth / LAN WebSocket
→ ESP32 NVS Flash：tmp
→ 通用 Skill 播放器
→ 舵机
```

### 2. Bottango Solar：ESP32 商品把动画作为独立数据文件离线播放

> 置信度：高——官方产品文档与开源 ESP32 驱动直接支持该结论。

Bottango Solar 是带 10 路舵机接口和 microSD 的 ESP32 动画控制板。Bottango 桌面软件负责时间线、关键帧和曲线设计，导出阶段可选择固件代码、SD 卡文件或 JSON；其中 SD 卡模式正是“固件不变、增加动画数据文件”。[S5][S6]

官方开源固件中的 `SDCardCommandStreamDataSource` 从 `/anim/` 文件读取动作命令，`CommandStreamProvider` 根据动画 ID 建立数据流，统一运行时解释曲线并驱动输出。[S7]

限制是 Bottango 当前仍要求把 microSD 插到电脑复制动画文件，尚未提供云端经 Wi-Fi 把动画文件直接写入 SD 卡的正式流程。因此它证明播放器/数据分离，但没有覆盖 Sesame 的无线资产发布环节。[S5]

### 3. Hiwonder miniHexa：第二个 ESP32 商品案例

> 置信度：高——官方硬件与动作编辑文档明确区分动作组下载和程序下载。

Hiwonder miniHexa 使用 ESP32-WROOM。电脑动作编辑器把每一帧的舵机位置和持续时间保存为 `.rob` 动作组，再把动作组下载到机器人控制器；通用程序随后用动作组编号调用，例如 `action_run(5)`。[S8]

这证明动作数据可以独立于 Python/固件程序更新。官方教程的下载示例主要使用 USB，虽然编辑器也提供 Wi-Fi 连接说明，但没有足够证据把它描述为互联网云端发布。[S8]

### 4. ROBOTIS MINI / R+ Motion：成熟的“代码与动作数据分离”产品模型

> 置信度：高——ROBOTIS 官方手册直接定义 Task 为程序、Motion 为动作数据。

R+ Motion 在 PC 或移动端编辑关节位置、速度、关键帧和动作流，提供离线 3D 机器人预览。用户选择 Motion Group 下载到机器人控制器，程序以后用 Motion Index 调用指定动作。[S9][S10]

ROBOTIS MINI 官方手册明确说明：Task Code 决定逻辑，Motion File 是动作数据；编辑后的动作只需下载到控制器一次，之后 App 或任务代码即可调用。[S10]

该控制器不是 ESP32，但其产品架构与 Sesame 目标完全相同。

### 5. Boston Dynamics Spot：无线上传、机器人校验、持久保存、按名称执行

> 置信度：高——Boston Dynamics 官方 SDK 完整公开上传、校验、保存和执行接口。

Spot 的 Choreographer 在电脑上组合动作和参数，Choreography API 把序列上传给机器人。机器人端服务会检查结构、参数范围和可行性，再用唯一名称执行；`SaveSequence` 能把序列及其依赖持久保存到机器人，重启后仍可从平板或 API 调用。[S11]

这与 Sesame 希望实现的网络资产发布最相似：

```text
电脑创作
→ 网络上传动作资产
→ 机器人端校验
→ 持久保存
→ 按名称执行
```

Spot 不是微控制器产品，但证明这套架构在高端商业机器人上同样成立，并且机器人端校验不能省略。

### 6. Alter3：LLM 自然语言设计动作已有实机研究证明

> 置信度：高——同行评审论文提供了 43 轴实机方法与生成流程。

东京大学与 Alternative Machine 的 Alter3 使用 GPT-4 把自然语言动作描述分两步转换成 43 个执行轴的 Python 控制代码，能够生成自拍姿势、扮鬼等未逐项预编程的动作，也能根据自然语言反馈修改姿势。[S12]

Alter3 证明的是 Sesame 的 OpenClaw 前半段——语言到动作方案——而不是多资产持久化产品管线。论文还明确指出低层控制与具体硬件强相关，这支持 Sesame 将 LLM 输出放在结构化草稿和校验器之前，而不是让模型绕过播放器直接控制舵机。[S12]

## 案例对比

| 案例 | 上位机设计 | 动作独立于固件 | 设备持久保存 | 本地按 ID/名称播放 | 无线传输 | ESP32 | 与 Sesame 的主要差异 |
|---|---|---:|---:|---:|---:|---:|---|
| Petoi Bittle X | Skill Composer / Web Blocks | 是 | 是，但动态单槽 | 是 | USB、蓝牙、LAN Wi-Fi | 是 | 只有一个 `tmp` 动态槽；不是云端 |
| Bottango Solar | Bottango 时间线 | 是，SD/JSON 模式 | 是，microSD | 是 | 当前资产复制需拔卡 | 是 | 缺云端无线资产写入 |
| Hiwonder miniHexa | PC Action Editor | 是，`.rob` | 是，控制器侧 | 是，group number | 官方示例主要 USB | 是 | 没有 LLM 与云端资产服务 |
| ROBOTIS MINI | R+ Motion | 是，Motion File | 是，控制器侧 | 是，motion index | 蓝牙/PC | 否 | 硬件不同；架构一致 |
| Boston Dynamics Spot | Choreographer | 是，sequence/animation | 是，机器人文件库 | 是，unique name | 是，网络 API | 否 | 计算平台更强、价格与复杂度更高 |
| Alter3 | GPT-4 文本生成 | 生成 Python/轴命令 | 未证明资产库 | 实机执行 | API + 串口 | 否 | 研究原型，只证明 LLM 前半段 |

## 对 Sesame 当前代码的对应

当前 Sesame 已有：

- WSS 控制协议和 `action.execute` / `expression.set` 接收；
- 网关与 ESP32 双重动作白名单；
- `RobotAdapter` / `RobotDriver` 抽象。

当前仍缺：

- 真正的舵机 `RobotDriver`；
- MotionClip/FaceClip 通用播放器；
- 动作资产下载器、注册表、版本/hash 与原子更新；
- 独立资产分区；
- 多动作存储和回滚。

关键本地证据：

- `/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF/main/app_main.cpp:33` 当前仍是 `RobotAdapter(nullptr)`。
- `/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF/partitions.csv` 只有 NVS、factory 和语音 model，没有 assets 与 OTA 双分区。
- `/Users/mac/Documents/sesame robot/.internal/research/2026-07-22-sesame-motion-expression-authoring.md` 已定义自然语言 → MotionClip → 校验 → 低速预览 → 发布的目标流程。

## 结论与建议

1. 继续采用“动作是数据、固件是播放器”的路线。这不是未经验证的新方法，而是成熟机器人产品的共同模式。
2. Sesame 第一版最值得借鉴 Petoi：通过现有 WSS 发送动作数组，并由 ESP32 Flash 保存和播放；但直接设计成多资产库，不复制 Petoi 的单 `tmp` 槽限制。
3. 播放器和动作数据格式可参考 Bottango：关键帧、时间、曲线、动作 ID、离线流式读取。
4. 发布与校验流程参考 Spot：上传草稿 → 设备/网关校验 → 保存 → 按名称执行，而不是上传后立即无条件运行。
5. OpenClaw 的自然语言生成可参考 Alter3，但输出应为 MotionClip 草稿或动作原语组合，不直接成为裸舵机命令。
6. 现有 4 MB 分区需要至少一次有线重刷，加入 assets 分区和通用播放器。动作资产无线更新不等于固件 OTA。

## 待解决问题

- 第一版资产库容量和单动作最大帧数。
- 使用内部 Flash/LittleFS，还是增加 microSD。
- 4 MB Flash 是否继续使用；若同时要求 A/B 固件 OTA，空间余量很紧。
- 第一版是否只允许动作原语组合，还是允许完整自由关键帧。

## 来源

- [S1 Petoi Skill Composer](https://docs.petoi.com/desktop-app/skill-composer) — 官方文档；关键帧创作、传输、临时技能掉电保存。
- [S2 Petoi OpenCatESP32](https://github.com/PetoiCamp/OpenCatEsp32-Quadruped-Robot) — 官方源码；ESP32 BiBoard 与通用 Skill 数据结构。
- [S3 Petoi 动态技能接收源码](https://github.com/PetoiCamp/OpenCatEsp32-Quadruped-Robot/blob/5022fb024eb37a3279147530dcf4b900194f5958/src/reaction.h#L1367-L1399) — 官方固定版本源码；NVS `tmp` 写入、重读与本地播放。
- [S4 Petoi Skill Creation](https://docs.petoi.com/applications/skill-creation) — 官方文档；单临时槽限制与多内置技能旧流程。
- [S5 Bottango Export Animations](https://docs.bottango.com/learn-bottango/exporting-animations/) — 官方文档；固件代码、SD 文件、JSON 三种导出。
- [S6 Bottango Control Boards](https://docs.bottango.com/bottango-hardware/controls/bottango-control-boards/) — 官方产品文档；Solar/Nova 的 ESP32、microSD 与离线播放。
- [S7 Bottango SDCardCommandStreamDataSource](https://github.com/EvanBottango/Bottango/blob/61d2980e560b2ddb4bcb68fcc596d93c4b2ff37e/BottangoArduinoDriver/src/SDCardCommandStreamDataSource.cpp#L8-L20) — 官方固定版本源码；SD 动作命令流播放器。
- [S8 Hiwonder miniHexa PC Action Editor](https://docs.hiwonder.com/projects/miniHexa/en/v1.2/docs/3_PC_Control_and_Action_Group_Editing.html) — 官方文档；ESP32 产品、`.rob` 动作组与控制器下载。
- [S9 ROBOTIS R+ Motion 2.0](https://emanual.robotis.com/docs/en/software/rplus2/motion/) — 官方文档；动作数据、3D 预览、Motion Group 下载与编号调用。
- [S10 ROBOTIS MINI](https://emanual.robotis.com/docs/en/edu/mini/) — 官方产品手册；Task Code 与 Motion File 分离。
- [S11 Spot Choreography Service](https://dev.bostondynamics.com/docs/concepts/choreography/choreography_service.html) — 官方 SDK；上传、校验、持久保存、删除和按名称执行。
- [S12 Alter3: From text to motion](https://www.frontiersin.org/journals/robotics-and-ai/articles/10.3389/frobt.2025.1581110/full) — 同行评审研究；GPT-4 自然语言到 43 轴实机动作。
