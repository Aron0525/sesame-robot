# P0 最小硬件隔离诊断记录

日期：2026-07-28  
设备：已连接的 ESP32-S3（16 MB Flash、8 MB 内嵌 PSRAM）

## 目标

用不初始化 Wi-Fi、I2S、OLED、舵机或 PSRAM 的最小 ESP-IDF 固件，隔离此前完整固件中的看门狗重启。

## 固件与烧录

- 工程：`firmware/esp32_voice_idf/diagnostics/p0_boot_diagnostic`
- ESP-IDF：v5.5.4
- 新建构建目录：`build-p0`
- Flash 配置：DIO、80 MHz、16 MB
- 烧录端口：USB Serial/JTAG

## 实机结果

串口启动日志确认了以下事件：

1. 启动日志显示 `SPI Flash Size : 16MB`、`SPI Mode : DIO`、`Boot SPI Speed : 80MHz`。
2. `P0_BEGIN` 后，以 1 秒节拍输出 `P0_HEARTBEAT`。
3. 第 60 秒输出：`P0_PASS: 60 seconds stable without initializing peripheral modules`。
4. 在 `P0_PASS` 后仍观察到第 61 至 68 秒的 heartbeat；全程没有复位或看门狗日志。

## 结论

P0 通过。芯片基础启动、Flash 配置、FreeRTOS 调度和 USB 串口不是此前看门狗的触发点。后续 P1/P2 将从完整固件中逐层验证外设初始化、网络/WSS 和语音任务并发。
