# Learning Record 0002 — Sesame V3 接线必须区分目标与实物证据

## Context

用户询问当前机器人接线，希望把代码中的 GPIO 设定变成可对照实物的理解。

## Current understanding to retain

- 正式目标信号：INMP441 使用 GPIO14/47/48，MAX98357A 使用 GPIO14/47/2/1，OLED 使用 GPIO8/9，八个舵机使用 GPIO4/5/6/7/10/11/12/13。
- I2S 时钟 BCLK/WS 可由麦克风和功放共享，但两者的数据线不能共享：INMP441 SD 输入 ESP32，MAX98357A DIN 从 ESP32 输出。
- 舵机有 signal、V+、GND 三线；高电流 5V_SERVO 与 ESP32 必须共地，ESP32 USB 不能替代舵机供电。
- 现有项目证据不等于完整实机接线证据：功放/扬声器记录为未安装，OLED/INMP441 还需实机验证，未保存硬件照片或可用串口证据。
- 板载 TPS54531 5V_SERVO 与 README 的“独立 5–6 V 电源”应理解为独立高电流轨；其 VSYS 真实供电路径仍待实物确认。

## Evidence

- `/Users/mac/Desktop/2/docs/hardware_modules.md`
- `/Users/mac/Desktop/2/firmware/esp32_voice_idf/README.md`
- `/Users/mac/Desktop/2/firmware/esp32_voice_idf/components/sesame_audio/include/sesame_audio/audio_contract.h`
- `/Users/mac/Desktop/2/firmware/esp32_voice_idf/components/sesame_robot/esp32_servo_driver.cpp`

## Next step

如用户提供设备正反面清晰照片，按模块把每一根实际线标为“已确认、接错、缺失或无法看清”，再决定是否进行最小上电验证。
