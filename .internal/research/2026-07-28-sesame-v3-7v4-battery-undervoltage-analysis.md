# 调研：Sesame V3 使用单块 7.4 V 电池时的欠压风险

> **日期：** 2026-07-28  
> **Bead：** 未创建（本环境未安装 `bd`）  
> **状态：** 已被 PCB 专项修订版取代
>
> **修订说明：** 后续已在上游仓库 `hardware/pcb/distro-v3` 找到 V3 原理图、BOM、Gerber 与 PCB 文件，用户也确认实际使用 SG90S。请以
> [`2026-07-28-sesame-v3-distro-v3-sg90s-power-verdict.md`](./2026-07-28-sesame-v3-distro-v3-sg90s-power-verdict.md)
> 为当前结论；本文保留为找到 PCB 前的条件分析记录。

## 摘要

单块 7.4 V 电池可以作为整机的上游能源，但不能直接接到 ESP32-S3、SG90、INMP441 或 MAX98357A 的电源脚；必须经过与各负载匹配的稳压电源轨。[S1][S2][S3][S4] 目前不能证明现有 PCB 在“8 路舵机、Wi-Fi、录音、OLED 和最大音量播放同时运行”时不会欠压，因为现有附件和仓库均没有 PCB 正反面照片、原理图、DC/DC 型号、电池持续放电能力或舵机准确型号。

在当前可核实的负载下，风险集中在 8 路舵机，其次是满功率音频和 Wi-Fi 发射瞬态。若 5 V 舵机轨只有 3–4 A、与 ESP32 共用细线/小稳压器，或电池/BMS 不能持续提供约 6 A 以上的电池侧电流，整机动作时出现舵机轨下陷、ESP32 重启或 BMS 断电的风险较高；“约 6 A”是基于 8 只 SG90 每只约 0.8 A 的条件化算例，不是对未知硬件的实测结论。[S1][S5][S6]

## 证据范围

### 已确认

- 当前正式固件使用 ESP32-S3、INMP441、MAX98357A、128×64 SSD1306 OLED 和 8 路舵机；8 路舵机 PWM 为 GPIO 4、5、6、7、10、11、12、13。[S7]
- 仓库明确要求舵机使用独立 5–6 V 电源轨并与 ESP32 共地，禁止由 USB 为八路舵机供电。[S7]
- 当前固件启用了 ESP32-S3 欠压检测，选择 `CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7`。[S7]

### 尚未确认

- 本轮没有收到可读取的 PCB 正反面照片；本机现有图片均为 Arduino、网页或架构界面截图。
- 仓库中没有 PCB、原理图、BOM、Gerber 或稳压芯片型号。
- 电池化学体系、容量 Ah、持续/峰值 C 倍率、BMS 持续/峰值电流和连接器额定电流未知。
- 舵机铭牌未入库。已有排障记录按 SG90/兼容舵机分析，但当前硬件清单仍将准确型号列为未知。
- 扬声器阻抗、额定功率以及实际播放音量未知。

因此，本文只能给出“满足哪些条件才不会欠压”和“怎样实测”，不能把 PCB 的带载能力写成已验证事实。

## 已核实的电气边界

| 负载 | 官方数据 | 对 7.4 V 供电的含义 |
|---|---|---|
| ESP32-S3 | 电源 3.0–3.6 V；Wi-Fi 802.11b 1 Mbps、21 dBm 发射峰值 340 mA。Espressif 建议 3.3 V 电源输出能力不低于 500 mA。[S1][S8] | 不能把 7.4 V 直接接到 3.3 V 电源轨；3.3 V 稳压器及布线至少要满足官方 500 mA 建议，并承受 Wi-Fi 瞬态。 |
| INMP441 | VDD 1.62–3.63 V；3.3 V 正常模式最大 2.5 mA。[S2] | 直接接 7.4 V 会超出额定范围；它本身不是主要功耗。 |
| MAX98357A | 单电源 2.5–5.5 V；5 V、4 Ω 时输出功率 3.2 W；静态电流 2.4 mA；在 1 W/8 Ω 条件下效率 92%。[S3] | 直接接 7.4 V 会超额定范围。3.2 W 输出对应的 5 V 输入电流理论下限为 0.64 A，实际还要计入损耗；按 92% 仅作算例约为 0.70 A，但该效率点不是 3.2 W/4 Ω 的保证值。 |
| SSD1306 驱动器 | 核心电源 1.65–3.3 V，SSD1306 common 最大 sink 电流为 30 mA。[S4] | 成品 OLED 模块可能带电荷泵、稳压和外围器件，不能只凭驱动芯片数字推算模块输入电流；需要模块型号或实测。 |
| TowerPro SG90 Digital | 官方页面标称 4.8 V、外部适配器供电；厂商在页面 QA 中答复 4.8–6 V 可用，工作电流约 0.5–2 A。[S5] | 8 只同时带载时，5 V 侧电流可能落在很宽的 4–16 A 区间。SG90 仿品很多，实际必须测。直接接 7.4 V/满电 8.4 V 不符合该电压边界。 |
| 7.4 V 2S 锂电池 | TI 的 2S QA/资料把 7.4 V 写为标称电压、8.4 V 写为充满/充电电压。[S9] | 如果用户的 7.4 V 电池确为 2S Li-ion/LiPo，PCB 输入端必须按至少 8.4 V 满电考虑；低端截止电压必须以电芯和 BMS 数据表为准，不能只用“7.4 V”推断。 |

## 条件化功耗计算

### 计算假设

- 舵机轨按 5.0 V。
- DC/DC 效率暂按 90% 演示。这个数字不是现有 PCB 的已知效率，最终必须替换为稳压芯片效率曲线或实测。
- 功放满输出输入功率暂按 `3.2 W / 0.92 = 3.48 W` 演示。92% 的官方测试点是 1 W/8 Ω，因此 3.48 W 只能作为近似量级。
- ESP32-S3 取官方 Wi-Fi 发射峰值 `3.3 V × 0.34 A = 1.12 W`。
- INMP441、OLED、开发板 USB/稳压损耗和 DC/DC 静态电流没有计入，所以结果不是完整上限。

公式：

```text
P_load ≈ 5 V × 8 × I_servo + P_amp + P_ESP32
I_battery ≈ P_load / (V_battery × η)
```

| 舵机条件 | 5 V 舵机电流 | 已计入负载功率 | 7.4 V 电池侧电流（η=90%） | 6.0 V 电池侧电流（η=90%） |
|---|---:|---:|---:|---:|
| TowerPro 页面下界：0.5 A/只 | 4.0 A | 约 24.6 W | 约 3.7 A | 约 4.6 A |
| Adafruit 8×SG90 QA 的堵转经验：0.8 A/只 | 6.4 A | 约 36.6 W | 约 5.5 A | 约 6.8 A |
| TowerPro 页面上界：2 A/只 | 16.0 A | 约 84.6 W | 约 12.7 A | 约 15.7 A |

这张表说明：

1. “7.4 V 能不能用”不是核心问题；只要有正确的 buck（降压）电源轨，7.4 V/8.4 V 可以降到 5 V 和 3.3 V。
2. 真正决定是否欠压的是电池/BMS、DC/DC、连接器、PCB 铜箔和线束能否承受瞬时电流。
3. 5 V/4 A 只能作为 Adafruit 所说“也许带 8 个舵机”的经验起点；当还要同时播放音频、Wi-Fi 发射并承受机械负载时，不能据此声明整机通过。[S6]
4. 5 V/10 A 的高质量 BEC/buck 可作为 SG90 训练机的首轮排障基准，但若舵机确实接近 TowerPro 页面给出的 2 A 上界，它也不是绝对最坏工况保证；最终规格必须由实测峰值决定。

## 欠压风险判断

### 高风险结构

- 7.4 V 电池直接接到舵机、MAX98357A、INMP441 或 ESP32-S3 的低压电源脚。
- 八路舵机和 ESP32 共用一个 3–4 A 的 5 V 小降压模块。
- 舵机大电流穿过 ESP32 开发板、面包板电源条、细杜邦线或单个小排针，再分给八路。
- 逻辑地和舵机地未在低阻公共点连接。
- 电池只有容量 mAh 标识，没有可验证的持续放电电流和 BMS 限流参数。
- 电池快没电时，buck 输入接近其欠压锁定/最大占空比边界，但 PCB 没有输入电压监测。

### 可以成立的单电池结构

```text
2S 7.4 V 电池（满电约 8.4 V）
  ├─ 保险/反接保护/总开关
  ├─ 高电流 5.0 V BEC/buck ──> S0–S7 舵机
  ├─ 独立 5.0 V 音频支路 ────> MAX98357A
  └─ 独立 3.3 V 稳压支路 ────> ESP32-S3 + INMP441 + OLED
                    所有地在电源分配点共地
```

“独立”指电源轨、稳压器和大电流回路分开，不一定要求第二块电池。单块 7.4 V 电池可以同时作为这些稳压支路的输入。

## 实机验收方法

### 需要记录的四个测点

1. 电池端子电压 `VBAT`。
2. buck 输入端电压 `VIN_BUCK`。
3. 最远舵机插头处红线对地的 `VSERVO`。
4. ESP32-S3 模块附近的 `V3V3`。

优先用示波器单次触发捕获动作开始瞬态；普通万用表可能看不到毫秒级下陷。另用电流钳或低阻分流器记录电池峰值电流，并监测 DC/DC、连接器和电池温升。

### 测试顺序

1. Wi-Fi 已连接、OLED 常亮、舵机不动作。
2. Wi-Fi 持续传输 + INMP441 录音。
3. 单路舵机在机械中位附近小幅动作。
4. 4 路同时动作。
5. 8 路执行实际最重动作。
6. 8 路最重动作 + Wi-Fi 上传 + MAX98357A 在计划最大音量播放。
7. 电池从满电到接近 BMS 正常截止前，重复第 6 项。

不要用人为卡死舵机做长时间测试。若要捕获堵转峰值，只能短时、受控并监测温度。

### 判定线

- `V3V3` 全程不得低于 ESP32-S3 官方 3.0 V 工作下限。[S1]
- 对 SG90，`VSERVO` 应保持在经舵机厂商确认的范围内；TowerPro 页面以 4.8 V 为标称，QA 答复 4.8–6 V 可用。[S5]
- MAX98357A 供电不得超过 5.5 V。[S3]
- 不得出现 BMS 断电、DC/DC 过流/过温保护、Wi-Fi 断线、OLED 黑屏、舵机抽动或音频明显爆音。
- 串口不得出现 `Brownout detector was triggered` 或意外复位。

当前固件的 `LVL_SEL_7` 对应官方文档中的约 2.44 V 欠压门限，而 ESP32-S3 的推荐工作下限是 3.0 V。[S10] 因此，“没有触发 brownout reset”不能证明 3.3 V 电源轨合格；必须直接量 `V3V3`。

## 需要补齐的数据

要把条件判断升级成“这块 PCB 可以/不可以”，需要：

1. PCB 正面和背面高清照片，芯片丝印、功率电感、二极管、MOSFET、保险器件、输入输出接口必须可读。
2. 7.4 V 电池铭牌：化学体系、容量 Ah、持续/峰值 C 倍率、BMS 持续/峰值放电电流、截止电压。
3. 8 只舵机的准确型号和铭牌。
4. MAX98357A 模块版本、扬声器阻抗与功率。
5. 若有，原理图、BOM、Gerber 和 DC/DC 模块链接。

## 结论

**当前不能判定“不会欠压”，而且如果现有 PCB 是单路 5 V/3–4 A 给所有模块供电，应按高风险处理。** 单块 7.4 V 电池可以作为整机唯一能源，但前提是满电 8.4 V 输入被正确降压，舵机大电流轨与逻辑轨分开，电池/BMS/稳压器/连接器/铜箔都满足实测峰值。

在尚未看到 PCB 和电池铭牌前，最合理的训练机基准是：5 V/10 A 级舵机 BEC/buck、独立且满足 Espressif ≥500 mA 建议的 3.3 V 逻辑轨、音频支路按约 0.7 A 以上量级核算，并用示波器按上述第 6 项工况验收。这个配置是排障起点，不替代板级实测。

## 来源

- [S1 ESP32-S3 Series Datasheet v2.2](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf) — Espressif 官方数据手册 | accessed 2026-07-28 | 3.3 V 条件下 Wi-Fi 峰值电流。
- [S2 INMP441 Datasheet Rev. 1.1](https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf) — TDK/InvenSense 官方数据手册 | accessed 2026-07-28 | 供电范围与正常模式电流。
- [S3 MAX98357A Product Page and Datasheet](https://www.analog.com/en/products/max98357a.html) — Analog Devices 官方资料 | accessed 2026-07-28 | 供电范围、输出功率、静态电流与效率测试点。
- [S4 Solomon Systech OLED Driver IC Selection Guide](https://www.solomon-systech.com/wp-content/themes/solomon-systech/documents/product-catalogs/oled-driver-ic.pdf) — Solomon Systech 官方资料 | accessed 2026-07-28 | SSD1306 电源边界和最大 sink 电流。
- [S5 TowerPro SG90 Digital](https://towerpro.com.tw/product/sg90-7/) — 舵机制造商页面及厂商 QA | accessed 2026-07-28 | 4.8 V、外部供电、0.5–2 A 工作电流答复、仿品警告。
- [S6 Adafruit：Powering Servos](https://learn.adafruit.com/adafruit-16-channel-pwm-servo-hat-for-raspberry-pi/powering-servos) 与 [8×SG90/ESP32 QA](https://forums.adafruit.com/viewtopic.php?t=176520) — 厂商工程指南与支持论坛 | updated/accessed 2026-07-28 | 多舵机电源量级、0.7–0.8 A 堵转经验、实测建议。
- [S7 Sesame V3 当前硬件清单与正式固件](file:///Users/mac/Desktop/1/SesameV3_%E8%AF%AD%E9%9F%B3%E6%9C%BA%E5%99%A8%E4%BA%BA%E9%A1%B9%E7%9B%AE/docs/hardware_modules.md) — 本地项目资料 | 2026-07-28 | 模块、GPIO、独立舵机电源约束与固件配置。
- [S8 ESP32-S3 Hardware Design Guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html) — Espressif 官方设计指南 | accessed 2026-07-28 | 3.3 V、≥500 mA 和去耦建议。
- [S9 TI E2E：2S 电池 7.4 V 标称/8.4 V 充电](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1120356/bq28z610evm-532-how-to-configure-battery-pack-of-2s-with-bq28z610) — TI 官方支持 QA | accessed 2026-07-28 | 2S 电池标称与充电电压。
- [S10 ESP-IDF ESP32-S3 Brownout Detector](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/kconfig.html) — Espressif 官方文档 | accessed 2026-07-28 | 欠压复位行为及 `LVL_SEL_7 ≈ 2.44 V`。
- [S11 Arduino Stack Exchange：多舵机电源 QA](https://arduino.stackexchange.com/questions/34398/arduino-uno-sensor-shield-powering-multiple-servos) 与 [舵机导致 brownout](https://arduino.stackexchange.com/a/94557) — 社区交叉核对 | accessed 2026-07-28 | 同时动作、起动峰值和实测电流的重要性；仅作经验支持，不替代数据手册。
