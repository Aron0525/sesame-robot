# Sesame Robot V3 ESP-IDF 正式固件

目标芯片：ESP32-S3，4 MB Flash。

## 固定音频参数

- 设备与电脑之间：Opus VOIP，16 kHz，mono，20 ms/packet。
- ASR/TTS 边界：PCM S16LE，16 kHz，mono。
- I2S 物理总线：16 kHz，32-bit stereo slots。INMP441 使用 I2S0 的 left
  slot（GPIO 14/47/48）；替换后的 MAX98357A 使用独立 I2S1（GPIO 1/2/3），
  接收复制到左右 slot 的 mono PCM。

之所以 I2S 总线使用 32-bit stereo，而网络仍是 mono，是因为 INMP441
在一个 32-bit slot 中输出 24-bit 麦克风样本；MAX98357A 兼容 32-bit stereo
slot。固件读取麦克风的 left slot 后转换为 mono PCM，播放时将 mono 样本以
0.5 倍增益复制到两个 slot，给功放保留余量。

## 固定引脚

| 信号 | GPIO |
|---|---:|
| INMP441 I2S0 BCLK / SCK | 14 |
| INMP441 I2S0 WS / LRCLK | 47 |
| INMP441 SD | 48 |
| MAX98357A I2S1 BCLK | 1 |
| MAX98357A I2S1 WS / LRC | 2 |
| MAX98357A DIN | 3 |

INMP441 的 L/R 接 GND，因此麦克风数据位于 left slot。

## 语音链路

- 不生成 WAV，也不把录音写入 Flash 或文件系统。
- 上行链路为 `I2S PCM → 固定 RAM 帧 → raw Opus → SSM1 → WSS`。
- 下行链路为 `WSS → SSM1 → raw Opus → 固定 RAM 帧 → I2S`。
- 下行播放使用 60 帧 Opus 抖动队列（1.2 秒容量），通常收齐 30 帧（600 ms）
  才启动；`tts.stop` 已到达但短句不足 30 帧时会播放现有帧。WSS 接收与 Opus
  解码/I2S 写入在不同 FreeRTOS 任务中，避免网络突发或 ACK 延迟阻塞扬声器节奏。
- 待机时，TFLite Micro 运行嵌入式自定义唤醒模型
  `zhima_wakeword_tts_v2_int8`（22,456 bytes，SHA-256
  `79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065`）。
  它以 1 秒音频窗口、200 ms 步长直接比较 raw score，阈值为 0.86；命中后由
  VAD 启动现有 Opus/WSS 对话轮次。ESP-SR 仅提供 VAD，内置 WakeNet 已禁用。
- BOOT（GPIO0）仍保留为手动对话入口：第一次按下开始录音，第二次按下结束；
  单次录音上限为 30 秒。
- 正式部署使用方法一：NVS 只保存 Wi-Fi、`gateway_id`、设备 Bearer token 和
  私有根证书；ESP32 通过 mDNS `_sesame-streamgw._tcp.local.` 发现电脑网关，再
  用发现到的 IPv4 地址建立 WSS。这样电脑的 DHCP 地址变化无需重烧配置。
- NVS 可选的 `gateway_url` 与 `gateway_tls` 只用于明确指定的临时固定端点；没有
  这两个键时即为正式 mDNS 模式。发现会查询三次，并逐个检查所有 IPv4/IPv6 记录，
  只使用与 `gateway_id`、TLS 和路径协议相符的 IPv4 网关。

## 构建

```bash
source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh
idf.py set-target esp32s3
idf.py build
```

## 烧录

本工程固定目标为 ESP32-S3、4 MB Flash。烧录流程采用 2026-08-21 已验证的稳定
定点更新方式：先构建、以 115200 波特率备份当前 NVS，再只写入并校验主程序。它不会
改动 bootloader、分区表或语音模型。脚本使用 ESP32-S3 的 ROM 下载器（`--no-stub`），
避免该板的 USB-Serial/JTAG 在加载 RAM stub 后掉线：

```bash
bash tools/flash.sh
```

如果电脑同时连接了多个串口设备，明确传入 ESP32 的端口：

```bash
bash tools/flash.sh /dev/cu.usbmodem101
```

脚本识别不到端口时，先换成支持数据传输的 USB 线并重新插拔开发板。ESP32-S3 未能
自动进入下载模式时，按住 BOOT（GPIO0），短按 RESET/EN，看到 `Connecting...` 后
再松开 BOOT。请不要把 Bluetooth 或 debug-console 端口作为烧录端口。

脚本会在项目外的 `~/Documents/sesame robot-backups/nvs/` 留下一份仅当前用户可读写的 NVS
备份。需要使用其他项目外位置时，设置 `SESAME_FLASH_BACKUP_DIR`。
默认不写入设备密钥；要在同一次烧录中更新每台设备的私密配置，明确提供 NVS 镜像：

```bash
bash tools/flash.sh --nvs /private/path/device-nvs.bin /dev/cu.usbmodem101
```

该镜像必须是 24 KiB；脚本会在 `0x9000` 写入并校验它。日常代码更新不要加 `--nvs`。

主机协议测试：

```bash
bash tests/run_host_tests.sh
```

## 新扬声器实机验收

本次替换后的 MAX98357A 接线必须是 GPIO 1（BCLK）、GPIO 2（WS/LRC）、GPIO 3
（DIN）；GPIO 1 不再用于 SD/EN。烧录前先执行上述主机测试与构建。烧录后，设备
连上 Voice Gateway 并收到一轮 `tts.start`、SSM1 下行 Opus 帧、`tts.stop` 时，
串口应出现 `I2S ready: mic GPIO 14/47/48 on I2S0; speaker GPIO 1/2/3 on I2S1`，
并能听到 TTS。此验收需要真实硬件，不能由编译结果代替。

## 每台设备的 NVS 配置

Wi-Fi 密码、设备 token 和私有 CA 不写入源码。复制
`provisioning/device-config.example.json` 到仓库外的私密目录，填入当前设备的
真实值，然后生成 24 KiB NVS 镜像：

```bash
source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh
python3 tools/generate_nvs.py /path/to/device-config.json build/device-nvs.bin
esptool.py --chip esp32s3 --port /dev/cu.usbmodem101 write_flash \
  0x9000 build/device-nvs.bin
```

固件本体和 NVS 分开烧录；更新程序不会要求把凭据提交到 Git。量产时应进一步
启用 Flash Encryption（Flash 加密）和 NVS Encryption（NVS 加密）。当前生成
器只避免源码和日志泄密，单独的 `device-nvs.bin` 仍包含敏感信息，权限设为
`0600`，用完后按设备资产流程保管或销毁。
