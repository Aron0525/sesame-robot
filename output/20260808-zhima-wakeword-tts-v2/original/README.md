# Sesame Robot V3 ESP-IDF 正式固件

目标芯片：ESP32-S3，4 MB Flash。

## 固定音频参数

- 设备与电脑之间：Opus VOIP，16 kHz，mono，20 ms/packet。
- ASR/TTS 边界：PCM S16LE，16 kHz，mono。
- I2S 物理总线：16 kHz，32-bit stereo slots。INMP441 使用 left slot，
  MAX98357A 同时接收复制到左右 slot 的 mono PCM。

之所以 I2S 总线使用 32-bit stereo，而网络仍是 mono，是因为 INMP441
在一个 32-bit slot 中输出 24-bit 麦克风样本；麦克风和功放又共用 BCLK/WS。
固件读取 left slot 后转换为 mono PCM，播放时将 mono 样本复制到两个 slot。

## 固定引脚

| 信号 | GPIO |
|---|---:|
| I2S BCLK / INMP441 SCK | 14 |
| I2S WS / LRCLK | 47 |
| INMP441 SD | 48 |
| MAX98357A DIN | 2 |
| MAX98357A SD/EN | 1 |

INMP441 的 L/R 接 GND，因此麦克风数据位于 left slot。

## 语音链路

- 不生成 WAV，也不把录音写入 Flash 或文件系统。
- 上行链路为 `I2S PCM → 固定 RAM 帧 → raw Opus → SSM1 → WSS`。
- 下行链路为 `WSS → SSM1 → raw Opus → 固定 RAM 帧 → I2S`。
- BOOT（GPIO0）消抖后作为切换按钮：第一次按下开始录音，第二次按下结束；
  单次录音上限为 30 秒。
- 设备优先使用 NVS 中的端侧 `gateway_url` 建立 WSS 长连接，并校验私有根证书
  和设备 Bearer token。`gateway_url` 必须是以 `wss://` 开头、以
  `/v1/device-stream` 结尾的地址；mDNS `_sesame-gw._tcp.local.` 只保留给
  局域网开发发现，不是正式部署依赖。

## 构建

```bash
source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh
idf.py set-target esp32s3
idf.py build
```

## 烧录

本工程固定目标为 ESP32-S3、4 MB Flash。直接在项目目录执行下面的脚本；它会先
构建，再自动识别唯一的 ESP32 串口并烧录 bootloader、分区表、主程序和语音模型：

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

烧录脚本不写入设备密钥；每台设备仍需按下文生成并单独烧录 NVS 配置。

主机协议测试：

```bash
bash tests/run_host_tests.sh
```

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
