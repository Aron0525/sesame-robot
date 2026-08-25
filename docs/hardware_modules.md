# Sesame V3 硬件模块清单与接线导出

范围：当前唯一正式设备端固件
[`firmware/esp32_voice_idf`](/Users/mac/Desktop/2/firmware/esp32_voice_idf)。
本清单只列机器人实体硬件；电脑端 Voice Gateway、ASR、TTS、OpenClaw 属于外部软件服务，不是机载模块。

## 模块总表

| 编号 | 模块 | 数量 | 接口 / GPIO | 用途 | 当前状态 |
|---|---|---:|---|---|---|
| M01 | ESP32-S3 开发板 | 1 | 内置 Wi-Fi/BLE、USB Serial/JTAG | 主控、网页、WSS、音频与舵机控制 | 已由实机日志确认：16 MB Flash、8 MB OPI PSRAM |
| M02 | INMP441 数字麦克风 | 1 | BCLK=14、WS=47、SD=48、L/R→GND | 录音输入 | 正式固件已配置；需以实机录音链路确认接线 |
| M03 | MAX98357A I2S 数字功放 | 1 | BCLK=1、LRC=2、DIN=3（I2S1） | 将 TTS 音频放大输出 | 新接线已写入正式固件；仍需实机播放验收 |
| M04 | 扬声器 | 1 | 接 MAX98357A 的扬声器输出端 | 播放 TTS | 随 M03 未安装 |
| M05 | SSD1306 兼容 I2C OLED，128×64 | 1 | SDA=8、SCL=9、地址 `0x3C`、400 kHz | 表情与状态显示 | 固件已配置；本轮尚未完成修正版实机显示验证 |
| M06 | SG90S 舵机 S0–S7 | 8 | GPIO 4、5、6、7、10、11、12、13 | 姿态、动作、网页调平 | 用户已确认实际型号为 SG90S；上游 BOM 默认 MG90S，不能混用电流参数 |
| M07 | Distro V3 板载舵机电源 | 1 | TPS54531：VSYS→`5V_SERVO`，与 ESP32 共地 | 承担八路舵机峰值电流 | 芯片额定 5 A 连续；按 R2=62 kΩ、R12=10 kΩ 计算，输出典型值约 5.76 V |
| M08 | BOOT 按键（ESP32-S3 板载） | 1 | GPIO 0，低电平按下 | 语音录音开始／结束；下载模式进入键 | 板载复用；烧录时必须按 ESP32-S3 下载流程操作 |
| M09 | USB 数据/供电线 | 1 | ESP32-S3 板载 USB Serial/JTAG | 烧录、串口日志、主控供电 | 2026-08-21 已通过 `/dev/cu.usbmodem101` 完成应用分区烧录与日志验证 |

## 接线表

| ESP32-S3 GPIO | 去向 | 信号 |
|---:|---|---|
| 0 | 板载 BOOT 按键 | 录音按钮 / 下载模式 |
| 1 | MAX98357A | BCLK，I2S1 位时钟 |
| 2 | MAX98357A | LRC / WS，I2S1 字选择 |
| 3 | MAX98357A | DIN，I2S 音频数据输出 |
| 4 | S0 | 舵机 PWM |
| 5 | S1 | 舵机 PWM |
| 6 | S2 | 舵机 PWM |
| 7 | S3 | 舵机 PWM |
| 8 | OLED | I2C SDA |
| 9 | OLED | I2C SCL |
| 10 | S4 | 舵机 PWM |
| 11 | S5 | 舵机 PWM |
| 12 | S6 | 舵机 PWM |
| 13 | S7 | 舵机 PWM |
| 14 | INMP441 | I2S0 BCLK / SCK |
| 47 | INMP441 | I2S0 WS / LRCLK |
| 48 | INMP441 | SD，I2S 音频数据输入 |

## 电源与总线关系

```text
7.4 V 电池 / USB-C PD → LTC4416 电源选择 → VSYS
                                          ├─ SY8120B1 → 3V3 → ESP32-S3
                                          │                  ├─ I2S → INMP441
                                          │                  ├─ I2C → SSD1306 OLED
                                          │                  └─ PWM → S0–S7 信号
                                          └─ TPS54531 → 5V_SERVO → S0–S7 电源

MAX98357A 的最终供电轨和扬声器规格仍需按实物接线补录。
```

## 关键约束

- INMP441 的 `L/R` 必须接 GND；固件只从左声道读取数据。
- MAX98357A 使用 I2S1 的 GPIO `1/2/3`；INMP441 使用 I2S0 的 GPIO `14/47/48`。两者不共享时钟，仍可同时录音和播放。
- OLED 使用 I2C 内部弱上拉；若显示不稳定，应确认模块自带或外接 SDA/SCL 上拉电阻。
- 舵机由 ESP32-S3 直接输出 50 Hz PWM，没有 PCA9685 或其他外置舵机驱动板。
- `5V_SERVO` 由板载 TPS54531 生成，芯片最大连续输出 5 A；这不是 8 路 SG90S 同时堵转的保证值。
- 当前 V3 原理图的 `5V_SERVO` 反馈电阻给出的典型电压约为 5.76 V，不应按精确 5.0 V 处理。
- 上电时固件将舵机 PWM 保持释放状态，不应自动摆动；网页动作与语音动作会互斥占用 PWM。
- `GPIO0` 同时是 BOOT 与录音按键。正常运行时不要长期拉低；烧录时按住 BOOT、短按 EN/RST、再松开 BOOT。

## 未纳入当前硬件的项目

- 摄像头、超声波／红外传感器、IMU、PCA9685 舵机驱动板、独立蓝牙模块、SD 卡模块均未被当前正式固件使用。
- 电脑端网关、ASR、TTS 和 OpenClaw 在电脑或云端运行，不应接入 ESP32-S3 的 GPIO。

## 依据

- 音频 GPIO 与 I2S 约束：`components/sesame_audio/include/sesame_audio/audio_contract.h`、`components/sesame_audio/audio_hal.cpp`。
- OLED 参数：`components/sesame_ui/oled_expression_display.cpp`。
- 八路舵机引脚与 PWM：`components/sesame_robot/esp32_servo_driver.cpp`。
- BOOT 按键与硬件目标：`components/sesame_voice/voice_controller.cpp`、`sdkconfig`。
