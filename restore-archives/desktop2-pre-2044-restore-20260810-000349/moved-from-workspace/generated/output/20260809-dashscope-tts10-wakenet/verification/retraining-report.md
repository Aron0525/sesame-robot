# DashScope TTS 十样本 WakeNet 重训练报告

## 结果

候选模型通过本次离线门禁，**尚未替换 `desktop/2` 中已烧录的模型，也未重新烧录单片机**。

- 当前部署模型：`zhima_wakeword_tts_v2_int8`，SHA-256 `79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065`。
- 新候选模型：`zhima_wakeword_dashscope10_int8`，SHA-256 `2247f91d43b4d70fe17c90b1e999130c2b51476e044a86156b4ea250ba6ae305`，22,456 bytes。
- 设备运行阈值按当前设置保持 `0.85`；推理窗口和 200 ms 推理间隔保持原契约。

## 数据

通过项目已配置的 DashScope TTS 合成了 10 段“你好，芝麻”，均为 16 kHz、单声道、PCM S16LE，WAV 哈希彼此不同。

- 第 1–8 段：训练正样本。
- 第 9–10 段：不参与训练的 DashScope 留出测试。
- 训练还保留原 TTS-v2 的 120 条正负样本、原有的“你好小智”硬负样本和一般负样本。

## 测试

| 测试 | 阈值 | 基线模型 | 新模型 |
| --- | ---: | ---: | ---: |
| 2 段未见 DashScope 留出音频 | 0.85 | 1/2 触发，50% | 2/2 触发，100% |
| 说话人/引擎隔离验证 | 0.85 | — | 8/8 正样本触发，32/32 负样本拒绝 |
| 独立引擎留出集 | 0.85 | — | 5/5 正样本触发，25/25 负样本拒绝 |
| TFLite 部署契约 | — | — | 输入 `[1,33,32,1]` int8、输出 int8、算子集合通过 |

候选资产已将新模型的特征均值 `-17.3126373`、标准差 `2.72947984`、输入量化 scale `0.0243355129`、zero point `-77` 和模型字节一起导出。嵌入 C++ 字节数组的 SHA-256 与原始 TFLite 完全一致。

## 测试边界

本次通过的是 TTS 离线门禁：8 段训练、2 段同引擎未见测试，以及原有的独立合成语音留出集。它证明新 TFLite 与 ESP32 特征契约匹配；真实 INMP441 麦克风、房间噪声和扬声器播放场景仍需要在单片机上复测后再替换当前模型。

## 文件

- 10 段音频与清单：`tts/manifest.json`
- 新模型：`model/zhima_wakeword_dashscope10_int8.tflite`
- 训练指标：`model/training_metrics.json`
- 测试结果：`verification/candidate-acceptance.json`
- ESP32 候选资产：`candidate-firmware-assets/`
- 合成、训练与测试日志：`verification/`

## 复跑

生成、训练和测试脚本在 `scripts/`。训练源数据路径和输出目录均通过环境变量传入，原训练工程与当前 `desktop/2` 固件模型没有被覆盖。
