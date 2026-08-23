# 调研：Sesame Robot 表情与 8 舵机动作设计

> 日期：2026-07-22  
> 状态：完成  
> 范围：GitHub 动作工具、国内外创客社区工作流、`dorianborian/sesame-robot` V3 固件  
> Bead：本机未安装 `bd`，跳过

## 摘要

Sesame 不应继续把动作写成一批手工 C++ 函数，也不应让 LLM 直接生成 8 路舵机命令。推荐路线是先定义统一动作 DSL：`关键帧时间 + 8 个舵机目标角 + 缓动 + 表情事件`，由可视化时间线或自然语言生成草稿，再经过确定性校验、低速预览和真机确认，最后由编译器生成 ESP32 数据。[S1][S8][S9]

V3 仓库已有 Sesame Studio，但目前只是角度输入和代码片段生成器；真正适合快速验证的外部工具是 Bottango。OLED 表情建议用 Pixelorama 画逐帧动画，再用 image2cpp 转成 128×64 单色位图；若只做单帧，可直接用 Lopaka。[S1][S4][S5][S6]

## 1. 判断与产品路线

### 1.1 推荐路线

> **置信度：高** — V3 源码、成熟机器人动画工具与研究论文的共同结论。

```text
自然语言 / 手工调姿
  → 受约束 MotionClip JSON 草稿
  → 时间线编辑与 2D/3D 预览
  → 角度、速度、姿态、电流风险校验
  → 10%/25%/50% 低速真机预览
  → 确认后保存为预定动作
  → ESP32 非阻塞播放器执行
```

自然语言适合表达“先低头，再向左右摇两次，同时显示害羞表情”，不适合直接决定未经校准的 SG90/MG90 角度。Code as Policies、Alter3 和表达性机器人动作研究都证明 LLM 可以组合动作 API 或产生参数化控制代码，但也强调低层控制与具体硬件强相关。[S8][S9][S10]

### 1.2 最小动作 DSL

```json
{
  "name": "shy",
  "version": 1,
  "loop": false,
  "keyframes": [
    {"t_ms": 0, "angles": [135,45,45,135,0,180,0,180], "easing": "linear"},
    {"t_ms": 400, "angles": [150,30,30,150,20,160,20,160], "easing": "ease_in_out"},
    {"t_ms": 900, "pose": "sway_left", "easing": "ease_in_out"},
    {"t_ms": 1400, "pose": "sway_right", "easing": "ease_in_out"},
    {"t_ms": 2200, "pose": "stand", "easing": "ease_in_out"}
  ],
  "expression_track": [
    {"t_ms": 0, "expression": "shy", "mode": "loop"},
    {"t_ms": 2200, "expression": "idle", "mode": "boomerang"}
  ]
}
```

每台机器人的校准 `min/max/home/offset/invert` 不属于动作内容，LLM 和普通用户不能修改。编译器还要检查相邻帧角度变化、最短时长、最大速度、同时大幅运动的舵机数、起止姿态和急停能力；具体阈值必须由真机测试得出。

## 2. GitHub 与现有工具

| 工具/项目 | 适用部分 | 许可证与状态（2026-07-22 核验） | 对 Sesame 的判断 |
|---|---|---|---|
| [Bottango](https://github.com/EvanBottango/Bottango) | 8 舵机关键帧、Bezier 曲线、实机预览、HID 示教、代码/JSON 导出 | 驱动/API BSD-3-Clause；桌面 App 另有 EULA；持续活跃 | **首选验证方案**。支持 Arduino/ESP32 与网络驱动；先做适配器，不要立刻自研完整编辑器。[S1][S2] |
| [Sesame Studio](https://github.com/dorianborian/sesame-robot/tree/main/software/sesame-studio) | 8 角度输入、帧延时、C++ 片段生成 | Apache-2.0；V3 仓库内置 | 只适合作为原型起点；没有时间线、插值、保存格式、实时预览、仿真和安全检查。[S3] |
| [MarIOnette](https://github.com/knee-koh/MarIOnette) | Blender 3D 骨架、关键帧、串口控制 ESP32 | GPL-3.0；2025-01-25 活跃 | 适合第二阶段 3D 所见即所得；Blender 学习和 GPL 集成成本较高。 |
| [OpenCatESP32](https://github.com/PetoiCamp/OpenCatEsp32-Quadruped-Robot) | ESP32 四足动作库、Skill/frame 数据模型 | MIT；2026-07 活跃 | 适合参考播放器和动作数据，不适合直接当 Sesame 编辑器。 |
| [ServoEasing](https://github.com/ArminJo/ServoEasing) | 多舵机同步、非阻塞缓动、角度约束 | GPL-3.0/商业许可；持续维护 | 适合参考或验证 runtime；正式产品需先解决许可选择。 |
| [Lopaka](https://github.com/sbrin/lopaka) | OLED 单帧设计、C/C++/XBMP 导出 | Apache-2.0；0.6 release 2026-03-16 | 单帧很好；动画时间线不如 Pixelorama。[S4] |
| [Pixelorama](https://github.com/Orama-Interactive/Pixelorama) | OLED 逐帧像素动画、洋葱皮、帧标签 | MIT；1.1.10 release 2026-04-29 | **表情动画首选创作工具**；导出 PNG/GIF 后需转固件位图。 |
| [image2cpp](https://github.com/javl/image2cpp) | 128×64 单色图转 byte array | GPL-3.0；2026-01 活跃 | 与 Pixelorama 配合；也正是 V3 README 指定的转换方式。[S5] |
| [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) | ESP32 运行时 GIF 解码 | Apache-2.0；2025-02 release | 能保留 GIF 帧时长，但对单色 OLED 复杂度和 RAM 开销高于静态帧表，不作为首版。 |

### 2.1 Bottango 为什么最匹配

> **置信度：高** — 官方文档和开源驱动仓库一致。

Bottango 把机器人动作建模成时间线关键帧，并在关键帧之间插值；真实硬件可以同步预览。官方还支持把录制输入拟合为可编辑关键帧/曲线，并导出固件代码、SD 文件或 JSON。[S1][S2]

对当前项目，先做一个小型适配实验：在 Bottango 中建立 8 个关节，映射 V3 的 `R1,R2,L1,L2,R4,R3,L3,L4`，通过 USB 或网络 driver 把目标角送到一个测试固件。验证通过后，再决定是长期使用 Bottango，还是把核心交互复刻进 Sesame Studio。

### 2.2 OLED 工作流

> **置信度：高** — V3 固件格式与工具能力直接匹配。

```text
Pixelorama：128×64、1-bit 风格、逐帧设计
  → 每帧 PNG / spritesheet
  → image2cpp 或自有转换器
  → frames[][1024] + frame_duration_ms[]
  → FaceClip 注册表
  → ESP32 非阻塞播放器
```

现有 V3 固件要求基础帧名为 `epd_bitmap_<name>`，后续连续使用 `_1..._5`，最多 6 帧；这不适合更长动画。产品化改造应改成显式 `frameCount` 和每帧时长，而不是依靠弱符号和命名约定。[S5]

## 3. 社交媒体与创客社区发现

> **置信度：中高** — 多个平台工作流一致，但部分平台只提供简介或搜索索引，无法获取完整字幕。

社区里成熟的低门槛方法高度一致：

1. **上位机滑杆 + Pose 保存**：逐个调 8 个舵机，保存一个姿态，再排列成动作组。国内 6/24 路舵机控制板、双足/机械臂项目普遍采用该方法。[S11][S12]
2. **关键帧 + 自动插值**：Bottango、NAO Choregraphe 等让用户只定义转折点，系统生成中间动作。[S1][S13]
3. **复制预设动作后修改**：Jimu/ClicBot 等消费级产品用拖拽或摆姿创建新动作，降低从空白开始的难度。[S14]
4. **示教录制**：高端总线舵机或带回读硬件可直接摆姿录制；普通三线 SG90 没有可靠位置回读，不应强行掰动齿轮。Sesame 首版应使用网页滑杆、手柄或虚拟模型，而非物理回拖。[S12]
5. **数字孪生**：3D/物理仿真可以减少真机风险，但首版只需简化骨架、角度范围、自碰/触地提示和低速预览，不必先建完整动力学。

平台限制：YouTube 多次限流；Bilibili 多数只能核验标题、简介和选集目录；部分知乎正文触发安全验证；微博和小红书公开可核验内容有限。因此这些社区信号用于发现工作流，承重技术判断仍以代码、官方文档和论文为主。

## 4. V3 当前有哪些动作

> **置信度：高** — 直接核验本机 commit `0aa04455d44c19e7d3dd1aaaf6b8b7bfb58df970`。

舵机数组顺序为 `[R1,R2,L1,L2,R4,R3,L3,L4]`；前四个是髋部，后四个是腿部。固件共有 19 个 movement command：4 个连续移动、15 个姿势/表演动作，另有 `stop`。

| 类型 | command | 代码表现 |
|---|---|---|
| 连续移动 | `forward` | 初始步 + 每周期 6 个离散阶段，默认 10 周期、阶段间隔 100 ms |
| 连续移动 | `backward` | 与前进相似，髋部目标顺序反转 |
| 连续移动 | `left` | 两组对角腿依次动作，每周期 8 阶段 |
| 连续移动 | `right` | 左转镜像，每周期 8 阶段 |
| 基础姿态 | `rest` | 8 舵机全 90° |
| 基础姿态 | `stand` | `[135,45,45,135,0,180,0,180]` |
| 表演 | `wave` | 站立后 L3 在 180°/100° 间摆 4 次 |
| 表演 | `dance` | 四个腿部舵机两组角度间摆 5 次 |
| 表演 | `swim` | 四个髋部在展开和全 90° 间摆 4 次 |
| 表演 | `point` | `[135,100,25,90,80,170,145,180]`，保持 2 s |
| 表演 | `pushup` | 前腿在伸展/弯曲间往复 4 次 |
| 表演 | `bow` | 前部下降，保持 3 s 后回站立 |
| 表演 | `cute` | 进入极限姿态后，R4/L4 往复 5 次 |
| 表演 | `freaky` | 髋部展开，R3 小幅摆动 3 次 |
| 表演 | `worm` | 四个腿部舵机以两组反相角度摆 5 次 |
| 表演 | `shake` | R4/L4 两组角度摆 5 次 |
| 表演 | `shrug` | 腿部全 90°，再展开，分别保持 1 s/1.5 s |
| 表演 | `dead` | 从站立变为四个腿部舵机全 90° |
| 表演 | `crab` | 髋部全 90°，腿部两组角度往复 5 次 |
| 控制 | `stop` | 清空 `currentCommand`；只会可靠中断四种行走动作 |

代码集中在 [`movement-sequences.h`](https://github.com/dorianborian/sesame-robot/blob/main/firmware/movement-sequences.h)，字符串分发位于 [`sesame-firmware-main.ino`](https://github.com/dorianborian/sesame-robot/blob/main/firmware/sesame-firmware-main.ino)。本机精确证据见：

- `/Users/mac/Desktop/行业调查/research/github_refs/sesame-robot/firmware/movement-sequences.h:5`
- `/Users/mac/Desktop/行业调查/research/github_refs/sesame-robot/firmware/movement-sequences.h:71`
- `/Users/mac/Desktop/行业调查/research/github_refs/sesame-robot/firmware/movement-sequences.h:91`
- `/Users/mac/Desktop/行业调查/research/github_refs/sesame-robot/firmware/movement-sequences.h:326`
- `/Users/mac/Desktop/行业调查/research/github_refs/sesame-robot/firmware/sesame-firmware-main.ino:493`

### 4.1 动作如何执行

```text
POST /api/command {"command":"wave"}
  → 手工字符串解析
  → currentCommand = "wave"
  → loop() 的 if/else dispatch
  → runWavePose()
  → 多次 setServoAngle(name, angle)
  → Servo.write(angle)
  → delayWithFace(...)
```

这不是关键帧播放器。`setServoAngle()` 立即写目标角，每写一个舵机后默认等待 20 ms，因此代码中看似同一帧的 8 个舵机实际上依次启动；没有速度规划或插值，真实运动速度由 SG90/MG90 自身决定。

动作函数阻塞主动作调度，但 `delayWithFace()` 会继续处理 HTTP/DNS 和 OLED。只有行走动作在每阶段调用 `pressingCheck()` 检查 stop；其他表演动作必须跑到函数末尾才能结束。[S3]

### 4.2 Sesame Studio 的真实能力

Studio 让用户填写 8 个角度和一个 delay，点击 Add Frame 后输出：

```cpp
setServoAngle(R1, ...);
// ...其余舵机
delay(...);
```

它没有播放/暂停、时间线、easing、保存/加载动作文件、实时机器人连接、仿真或碰撞/稳定性检查；还生成裸 `delay()`，而固件当前使用 `delayWithFace()` 维持 OLED 与网络刷新。它甚至存在舵机颜色表整数键与字符串 ID 不匹配的问题。[S3]

## 5. V3 当前有哪些表情

注册表包含 37 个名称：

- 动作类：`walk, rest, swim, dance, wave, point, stand, cute, pushup, freaky, bow, worm, shake, shrug, dead, crab`
- 特殊类：`defualt, idle, idle_blink`
- 对话类：`happy/talk_happy, sad/talk_sad, angry/talk_angry, surprised/talk_surprised, sleepy/talk_sleepy, love/talk_love, excited/talk_excited, confused/talk_confused, thinking/talk_thinking`

真正拥有连续可达多帧的只有：`rest` 3 帧、`dance` 2 帧、`point` 3 帧、`dead` 3 帧、`idle_blink` 4 帧。`thinking_2` 存在但 `_1` 缺失，播放器遇到空帧即停止计数，所以 `_2` 永远不可达；`stand` 与拼错的 `defualt` 没有基础位图。多数所谓动画实际只有一帧。[S5]

播放器支持：

- `LOOP`：循环；
- `ONCE`：播放一次并停在末帧；
- `BOOMERANG`：正向播放后反向播放。

表情由自己的 FPS 推进，与动作角度没有共享时间线。动作函数只在开始时切换表情，随后两者各自运行。[S5]

## 6. V3 需要先改什么

### P0：动作数据化与非阻塞播放器

```cpp
struct MotionFrame {
  uint16_t atMs;
  uint8_t angles[8];
  uint8_t easing;
};

struct MotionClip {
  const char* name;
  const MotionFrame* frames;
  uint16_t frameCount;
  bool loop;
};
```

播放器在每轮 `loop()` 用 `millis()` 计算当前进度和 8 路插值输出；所有动作可立即 stop；命令用表查找，不再新增一条动作就修改函数声明、dispatch、CLI 和 Web UI。

### P1：升级 Sesame Studio

首版只做：

- 8 路滑杆与校准后的合法范围；
- Pose 保存、复制和模板继承；
- 关键帧时间线；
- linear/ease-in-out/Bezier；
- OLED 表情轨；
- 10%/25%/50%/100% 预览；
- 导入/导出 MotionClip JSON；
- 编译成固件静态数据。

### P2：自然语言动作草稿

LLM 只能输出 JSON Schema 允许的字段，只引用合法 Pose、舵机名称和 easing；生成后必须通过校验、预览和人工确认。运行时 OpenClaw 仍只从已审核的动作枚举中选择，不能临时生成并直接执行新轨迹。

## 7. 已发现的风险与缺陷

- 当前 BOM/README 是 MG90，用户计划使用 SG90；两者齿轮、力矩和耐久度不同，角度极限与负载必须重新验证。
- V3 源码默认仍启用 Lolin S2 Mini 的 pin 配置，V3 引脚定义被注释，烧录前必须核对。
- 未知 command 会被 API 接受并返回 executed，但主循环没有匹配分支。
- `{"command":"wave","face":"happy"}` 中的 happy 会被动作函数随后切换成 wave 表情。
- Web UI 的 Motor Speed 会发送参数，但固件不读取。
- 表演动作无法可靠急停，不适合让 AI 直接触发未经验证的新轨迹。
- SG90 无标准位置回读；不要把“手掰示教”作为默认交互。

## 8. 推荐实施顺序

1. 用 Bottango + 8 路测试固件完成一个 `wave` 和一个新 `shy` 动作，验证编辑体验与导出格式。
2. 同时把 V3 `movement-sequences.h` 中 15 个姿势动作转成 MotionClip JSON/静态数据，保持现有视觉效果。
3. 实现非阻塞播放器、所有动作急停、角度/速度校验和低速预览。
4. 将 Sesame Studio 升级为 Pose + 时间线编辑器，或先做 Bottango 导入器。
5. 使用 Pixelorama → image2cpp 建立 OLED 动画资产管线，并移除 6 帧硬限制。
6. 最后加入自然语言生成 MotionClip 草稿；OpenClaw 运行时仍只调用已审核动作。

## 9. 各方案详细说明

### 9.1 Bottango：最快完成动作创作闭环

**定位：**完整机器人动画创作工具，不只是舵机库。它把机器人抽象为 Structure、Joint、Motor 和 Hardware Driver；用户在时间线上给关节建立关键帧，用 Graph View 修改插值曲线，并让虚拟模型和真实舵机同步运动。[S1][S2]

**接入 V3 的两种方式：**

1. 实时模式：电脑运行 Bottango，V3 刷入 Bottango Arduino driver 或自定义 network driver；Bottango 持续下发 8 路目标值。适合创作、调试和反复预览。
2. 脱机模式：从 Bottango 导出 firmware code、SD card files 或 JSON，再转换成 Sesame MotionClip。适合量产设备脱离电脑运行。

**V3 适配工作：**建立 `R1,R2,L1,L2,R4,R3,L3,L4` 八个 motor；填写每个关节 signal range、home、reverse 和机械限位；先只接测试固件，不直接覆盖正式 V3 固件；完成后写 JSON→MotionClip 转换器。

**优点：**关键帧、Bezier、Graph View、音频轨、游戏手柄录制、实时真机、导出都已存在；可快速判断“动作编辑”是否真的解决用户痛点。

**局限：**桌面应用为免费 beta，不等于整个应用开源；开源仓库主要是驱动和 API，桌面应用受单独 EULA；官方也提示导出模式受 MCU 存储限制，任意动作之间还不能自动无缝 blend。它适合做创作工具和技术验证，不应成为产品运行时的唯一依赖。

**本项目判断：**第一阶段首选。先验证 `wave` 和新建 `shy`，不要立即重写一套时间线编辑器。

### 9.2 MarIOnette：用 Blender 做 3D 所见即所得

**定位：**Blender 插件，把骨骼/对象轴映射到 Arduino/ESP32 actuator，通过串口让 Blender 中的姿态实时控制实体机器人。[S15]

**工作方式：**导入或建立 Sesame 3D 模型和 armature；每条腿建立两个旋转关节；在 MarIOnette 中添加 8 个 actuator 并绑定 bone/axis/pin；Sync 生成 Arduino template；通过 Blender 时间线、Graph Editor 和骨骼关键帧创作动作。

**优点：**3D 模型能帮助理解四足机器人的左右、前后和关节方向；Blender 的关键帧、曲线、复制、镜像和动画组织能力成熟；适合设计复杂协调动作和展示数字孪生。

**局限：**需要制作准确骨架、轴方向和舵机映射；Blender 学习成本明显高于 8 个滑杆；串口模板仍需与 V3 的 Wi-Fi、OLED、急停和 MotionClip 播放器整合；GPL-3.0 对分发和商业产品集成需要单独评估。

**本项目判断：**只有当 2D/时间线无法表达复杂动作，或需要高质量 3D 预览时再采用。不是第一阶段首选。

### 9.3 OpenCatESP32：参考四足动作的数据组织

**定位：**完整 ESP32 四足机器人框架，面向 Petoi BiBoard 和最多 12 个舵机；仓库包含 `SkillLibrary`、网页编程块和运行时。[S16]

**可借鉴内容：**将站立、坐下、步态和表演动作作为 skill 管理；使用紧凑的关节角数组和动作头信息；让命令只引用 skill 名称；把编辑/生成的数据交给统一播放器，而不是每个动作写一个 C++ 函数。

**不应直接搬用：**Petoi 的舵机数量、索引、零位、运动学、板卡和 IMU 逻辑与 Sesame 不同；直接移植整个框架会把 V3 变成 OpenCat 的分叉版本，耦合过重。

**本项目判断：**把它当数据结构和播放器的参考样本，不把它当编辑器或依赖框架。

### 9.4 ServoEasing：解决“两个关键帧之间怎么平滑移动”

**定位：**Arduino 舵机插值运行库，不负责设计 UI。它提供 linear 及多种 easing、多舵机同步/独立运动、非阻塞启动、stop/resume、trim、reverse 和 min/max 约束；ESP32 需配合 ESP32Servo。[S7]

**在 V3 中的用法：**当 MotionClip 进入下一关键帧时，为 8 个舵机设置目标角、duration 和 easing；由库或自研调度器按定时器更新，而不是 `Servo.write()+delay()`。

**优点：**能快速替换当前跳变式动作；现成的 quadruped 示例有参考价值；非阻塞和同步更新正好解决 V3 当前的核心缺陷。

**局限：**它不知道机器人的触地、碰撞、稳定和电流；“曲线平滑”不等于“动作安全”；部分非阻塞能力受平台限制。库为 GPL-3.0，并提供商业许可入口，产品化前要决定购买许可、遵守 GPL，或自研一个小型插值器。

**本项目判断：**适合原型验证。正式固件更推荐自研很小的 MotionClip interpolator，降低许可和框架耦合。

### 9.5 Pixelorama：负责画 OLED 动画，而不是生成固件

**定位：**MIT 像素动画编辑器，提供 layer/frame timeline、onion skinning、音频同步和 frame tags。[S6]

**建议工作流：**创建 128×64 项目；只使用黑白两色；一条表情动画建立若干帧；使用洋葱皮控制眼睛/嘴巴的细微变化；给帧设置标签；导出逐帧 PNG 或 spritesheet。

**优点：**比在 C 数组里改像素直观；真正支持逐帧动画和预览；开源、MIT、跨平台。

**局限：**不了解 SSD1306 的位序、PROGMEM 和 AdafruitGFX；不能直接生成 V3 `face-bitmaps.h`；导出的彩色/GIF 数据还必须变成 1-bit 128×64 位图。

**本项目判断：**表情动画创作首选，后面必须接转换器。

### 9.6 image2cpp：图片到 OLED 字节数组的转换器

**定位：**将图片转换成单色 OLED byte array，也能反向把数组恢复成图片，作者明确以 128×64 单色 OLED 为目标。[S5]

**用法：**把 Pixelorama 每一帧 PNG 转成 `const unsigned char[] PROGMEM`；确认方向、阈值、位序和颜色反转；再由脚本生成 FaceClip 帧表。

**优点：**与 V3 当前 `drawBitmap()` 数据格式直接接近；适合快速手工验证。

**局限：**它不是动画工具；多帧批量处理、帧时长、命名和注册仍需自己做；工具是 GPL-3.0。产品开发最好借鉴输出格式并写一个仓库内的小型转换脚本，避免每次手工网页转换。

### 9.7 Lopaka：更适合静态表情和布局

**定位：**面向 U8g2、AdafruitGFX、TFT_eSPI 等嵌入式显示库的可视化图形编辑器，可生成 XBMP 和 C/C++ 源码，Apache-2.0。[S4]

**适用场景：**设计 `idle`、错误图标、配网界面、电量图标等单帧画面；快速放置文本、线条、矩形和导入图像；直接观察生成代码。

**不适用场景：**长表情动画、洋葱皮、逐帧节奏和动作同步。它不能替代 Pixelorama 的动画时间线。

**本项目判断：**静态脸和系统 UI 首选；动态脸使用 Pixelorama。

### 9.8 Sesame Studio：最贴合 V3，但当前能力最弱

**当前实现：**Tkinter 单文件工具，在示意图上填写 8 个角度和 delay，点击 Add Frame 后生成 `setServoAngle()` 和 `delay()`，再复制到 Arduino IDE。[S3]

**保留价值：**已经使用 Sesame 的关节名称、角度范围和示意图；代码简单，适合快速改造；无需让普通用户学习 Blender 或 Bottango。

**必须补齐：**结构化 Project/Pose/MotionClip 文件；帧列表和时间线；修改/删除/复制关键帧；easing；实时连接 V3；低速预览和急停；校准配置；OLED 表情轨；导出 JSON 而不是裸 C++；仿真或至少姿态警告。

**本项目判断：**长期产品方向，但不是当前马上依赖的成熟工具。先用 Bottango 验证交互和数据模型，再按产品需求重做 Sesame Studio 2.0。

### 9.9 最终组合，而不是八选一

这些方案位于不同层，真正推荐的是组合：

```text
近期动作创作：Bottango
长期产品编辑：Sesame Studio 2.0
3D 高级预览：MarIOnette/Blender（可选）
动作数据参考：OpenCatESP32
固件插值验证：ServoEasing
动画表情绘制：Pixelorama
静态表情绘制：Lopaka
位图转换：仓库内转换器（原型期可用 image2cpp）
```

选择标准：需要最快看到真机效果用 Bottango；需要面向普通消费者的自有体验做 Sesame Studio 2.0；需要复杂 3D 骨架动作再上 MarIOnette；OpenCat 和 ServoEasing 主要服务开发者，不直接暴露给最终用户。

## 来源

- [S1 Bottango Docs](https://docs.bottango.com/) — 官方；关键帧、Bezier、实时硬件与安全说明。
- [S2 Bottango Drivers/API](https://github.com/EvanBottango/Bottango) — 官方 GitHub；Arduino/网络驱动、REST API、BSD-3-Clause。
- [S3 Sesame Robot](https://github.com/dorianborian/sesame-robot) — 官方 GitHub；V3 固件、Sesame Studio、动作与 API。
- [S4 Lopaka](https://github.com/sbrin/lopaka) — 官方 GitHub；OLED 图形编辑和 C/C++ 生成。
- [S5 Sesame face-bitmaps.h](https://github.com/dorianborian/sesame-robot/blob/main/firmware/face-bitmaps.h) — 官方源码；表情注册和位图帧。
- [S6 Pixelorama](https://github.com/Orama-Interactive/Pixelorama) — 官方 GitHub；逐帧像素动画和多格式导出。
- [S7 ServoEasing](https://github.com/ArminJo/ServoEasing) — 官方 GitHub；多舵机同步、非阻塞缓动、角度约束。
- [S8 Code as Policies](https://code-as-policies.github.io/) — 研究项目；自然语言生成机器人策略代码与 waypoint policy。
- [S9 From Text to Motion: Alter3](https://arxiv.org/abs/2312.06571) — 论文；文本到机器人动作及硬件依赖边界。
- [S10 Generative Expressive Robot Behaviors](https://arxiv.org/abs/2401.14673) — 论文；自然语言到参数化技能控制代码。
- [S11 Bilibili 24 路舵机控制板教程](https://www.bilibili.com/video/BV1ep4y1m7bg/) — 社区/厂商教程；动作组编辑、执行与示教目录。
- [S12 Simon Says, But With Servos](https://hackaday.com/2020/04/07/simon-says-but-with-servos/) — 创客社区；示教记录与回放。
- [S13 NAO Choregraphe Timeline](https://fileadmin.cs.lth.se/robot/nao/doc/software/choregraphe/panels/timeline_panel.html) — 机器人时间线、关节角关键帧与插值。
- [S14 Jimu 机器人评测](https://www.zhihu.com/tardis/bd/art/27479039) — 国内社区；拖拽创建动作与复用预设动作。
- [S15 MarIOnette](https://github.com/knee-koh/MarIOnette) — Blender 关键帧、Arduino/ESP32 串口映射与 GPL-3.0。
- [S16 OpenCatESP32](https://github.com/PetoiCamp/OpenCatEsp32-Quadruped-Robot) — ESP32 四足框架、SkillLibrary 与最多 12 舵机。

## 未解决问题

- 用户最终采用 SG90 还是仓库默认 MG90；这会改变可靠负载与寿命。
- 是否接受 Bottango 桌面 App 的 EULA，或只借鉴交互后自研 Sesame Studio。
- V3 的机械限位、最大安全角速度、同时运动电流和稳定姿态仍需实测。
- 产品首版是否需要 3D 物理仿真，还是 2D 骨架 + 真机低速预览已经足够。
