# INMP441 音频采集测试

本草图用于 V3 的 ESP32-S3 验证 INMP441 是否真的采到声音。它提供两层证据：

1. 串口每 250ms 输出 RMS、峰值与 dBFS；说话或拍手时数值必须明显变大。
2. 输入 `r` 后录制 5 秒单声道 16kHz/16-bit WAV 到 SPIFFS；用附带脚本下载并在电脑播放，确认录到的是声音而不是随机噪声。

## 接线

| INMP441 | V3 扩展排针 |
| --- | --- |
| `VDD` | `3V3` |
| `GND` | `GND` |
| `L/R` | `GND`（左声道） |
| `SCK` | `IO14` |
| `WS` | `IO47` |
| `SD` | `IO48` |

`VDD` 不能接 `5V`。本测试不使用 MAX98357A，避免扬声器反馈干扰麦克风验证。

## Arduino IDE 设置

1. 安装 Espressif 的 `esp32` 开发板包，版本 `3.3.10` 已验证可编译。
2. 选择 **ESP32S3 Dev Module**。
3. 选择 `Flash Size: 16MB`、`PSRAM: OPI PSRAM`、`Partition Scheme: Custom`。本目录的 `partitions.csv` 会提供 3.375MB 的 SPIFFS；不要选 `ESP SR 16M`，它包含第二个 SPIFFS 子类型分区，会使 Arduino 的 `SPIFFS.begin()` 无法确定要挂载哪一个分区。
4. 打开 `INMP441_Audio_Test.ino`，上传后以 `115200` 波特率打开串口监视器。

启动后应看到：

```text
INMP441_TEST|boot
I2S|BCLK=14|WS=47|DIN=48|rate=16000|slot=32bit_stereo
LEVEL|...|...|...|QUIET
```

安静时的数值通常较低；对麦克风说话、拍手或播放声音时，`RMS` 和 `PEAK` 应显著增大，状态通常变成 `SOUND`。只要数值随真实声音变化，就证明 I²S 接线与采样有效。

## 录制并回听

在串口监视器发送 `r`，等待：

```text
REC_DONE|samples=80000|bytes=160000|...
```

关闭串口监视器（它会占用端口），然后在此目录运行：

```bash
python3 -m pip install -r requirements.txt
python3 capture_inmp441_wav.py --port /dev/cu.usbmodem101 --record
```

下载脚本只接受本草图生成的单声道 16 kHz / 16-bit PCM WAV，并在完整校验后原子替换输出文件；串口截断或错位不会覆盖已有的有效录音。

这会生成 `inmp441_test.wav`。用 Finder、QuickTime 或任意播放器播放；能听到刚才的语音，即完成录音验收。

Windows 示例：

```powershell
python capture_inmp441_wav.py --port COM7 --record
```

## 常见失败定位

| 现象 | 优先检查 |
| --- | --- |
| 持续 `ERR|i2s_read` | `SCK`、`WS`、`SD` 是否接反或断线；是否共地。 |
| `RMS=0`、`PEAK=0` 不变 | `VDD` 是否为 3V3；`L/R` 是否接 GND；代码是否仍使用 IO14/47/48。 |
| 数值有变化，但 WAV 无人声 | 确认 INMP441 的拾音孔没有被遮住；让人声靠近麦克风再录一次。 |
| `ERR|spiffs_mount` | 选择 `Partition Scheme: Custom`，并确认草图目录中的 `partitions.csv` 没有被删除。 |
