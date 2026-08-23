# DashScope TTS 十样本 WakeNet 烧录验证

## 已写入设备

- 设备：ESP32-S3，`/dev/cu.usbmodem101`，MAC `28:84:85:a4:ef:1c`。
- 写入模型：`zhima_wakeword_dashscope10_int8`。
- 模型 SHA-256：`2247f91d43b4d70fe17c90b1e999130c2b51476e044a86156b4ea250ba6ae305`。
- 应用镜像 SHA-256：`3d2b0f76bb91f8a5c0586acc8d71b44851d051eb465927f6f60290013ed194cf`。
- 阈值：`0.85`；推理间隔：200 ms。

## 验证

- `new-model-asset-verifier.log`：源 TFLite、嵌入 C++ 字节、特征参数与模型名一致，退出码 0。
- `host-tests.log`：主机测试退出码 0。
- `idf-build.log`：ESP-IDF 构建完成，退出码 0。
- `flash.log`：四个分区写入完成，逐段显示 `Hash of data verified`，退出码 0。
- `flash-readback.log`：bootloader、分区表、应用、srmodels 均显示 `verify OK (digest matched)`，退出码 0。
- `device-boot.log`：设备启动时输出 `custom model ready: zhima_wakeword_dashscope10_int8, 22456 bytes, threshold=0.85`，并收到 `session.ready`。
- `gateway-device-snapshot-post-boot.json`：`dev_001` 在线且状态为 `ready`。

## 回滚

`../rollback.sh --dry-run` 只展示回滚操作。无参数执行时恢复五个旧模型资产文件，写入并回读原有四个分区。
