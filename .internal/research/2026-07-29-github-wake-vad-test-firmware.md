# ESP32 唤醒词 + 自动结束测试版实施记录

日期：2026-07-29

## 目标

将现有“按键开始/按键结束”录音流程改为测试版“唤醒词 + 说话检测 + 静音自动结束”，并构建、尝试烧录到项目的 ESP32-S3。

## 上游实现与许可

采用 Espressif 官方示例的 AFE（音频前端）feed/fetch 接入模式，而非整段复制：

- 示例：[esp-skainet `wake_word_detection/afe`](https://github.com/espressif/esp-skainet/tree/1741f001ad97eb948becf032698a4710633392bd/examples/wake_word_detection/afe)，其 `main.c` 为 CC0。
- 运行组件：[ESP-SR 2.4.7](https://components.espressif.com/components/espressif/esp-sr/versions/2.4.7/readme?language=en)，组件许可为 [MIT](https://github.com/espressif/esp-sr/blob/2f8c4b0459db5bbb39abd77adae27962d6d94bcb/LICENSE)。

复用的是 `esp_srmodel_init`、AFE 配置、`feed`、`fetch` 和 WakeNet/VAD 状态读取的官方接口模式。项目自身保留 I2S、Opus、WSS 协议、按键兼容和轮次状态机。

## 测试版行为

```text
等待唤醒词（“你好小智”）
  -> 检测到唤醒词，最多等 3 秒等用户开口
  -> VAD 检测到讲话，发送 listen.start 并开始上传 Opus
  -> 连续静音 800ms，发送 listen.stop
  -> 说话最多 10 秒，强制结束
```

保留 GPIO0 按键。按键启动后同样可由 VAD 静音自动结束；自动结束后需先完整松键再重新按下，避免去抖状态造成误触发。

## 代码落点

- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/wake_vad_engine.cpp`：WakeNet + VAD 的 ESP-SR 适配器。
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_turn_detector.cpp`：无硬件依赖的轮次状态机。
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_controller.cpp`：将音频流、检测事件和既有 WebSocket 轮次接在一起。
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_protocol/turn_state.cpp`：增加端点事件。
- `main/idf_component.yml`：固定 `espressif/esp-sr` 至 `2.4.7`。
- `partitions.csv`：将未被代码使用的 `storage` 分区改名为 `model`，大小保持 960KB。

模型为 `vadnet1_medium`（281.16KB）及 `wn9s_nihaoxiaozhi`（122.84KB），总计约 404KB，低于模型分区容量。

## 验证

- `bash tests/run_host_tests.sh`：通过（包括新增的轮次和自动结束按键测试）。
- ESP-IDF 5.5.4 完整构建：通过；`sesame_robot_v3.bin` 约 1.50MB，3MB app 分区剩余约 50%。
- 构建因工作区路径包含空格而无法正常链接，因此在 `/tmp/sesame_robot_idf_source` 的 APFS 克隆副本中构建相同源文件，规避了工具链路径解析问题。
- 已两次尝试向 `/dev/cu.usbmodem101` 烧录（460800 与 115200）。芯片识别为 ESP32-S3 QFN56、8MB 内嵌 PSRAM，但写入前串口断开。

## 烧录阻塞

`lsof /dev/cu.usbmodem101` 显示本地 `sesame-voice-gateway`（PID 82194）正持有串口。未终止它，以避免中断正在运行的网关。停止该进程或释放串口后，使用已生成的 `sesame_robot_v3.bin`、`partition-table.bin`、`srmodels.bin` 即可重试。

## 非目标和后续

- “你好小智”是官方测试模型固定词，不是产品最终唤醒词；产品词需要采购/训练相应 WakeNet 模型后替换 Kconfig 模型选项。
- 这次只能验证构建及模型打包；需在串口释放、烧录成功后，实机说出唤醒词并观察 `wake word detected` 和 `VAD endpoint reached` 日志，才能验证声学链路。
