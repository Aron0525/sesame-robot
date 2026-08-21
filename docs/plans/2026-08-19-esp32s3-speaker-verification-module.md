# ESP32-S3 说话人验证模块实施计划

> **For Codex:** Use `/Users/mac/.codex/skills/executing-plans/SKILL.md` to implement this plan task-by-task.

**Goal:** 在 ESP32-S3 固件中加入可独立管理、默认关闭、可从本机控制台快速开启或关闭的本地说话人验证模块，并移除当前产品文案和功能命名中的禁用词。

**Architecture:** 保留 ESP-SR MultiNet 负责“你好芝麻”口令检测；新增纯 C++ `SpeakerVerification` 组件，对命中前最近 1.5 秒的 16 kHz PCM 做定长声学特征和余弦相似度比较。用户开关与模板可用性分离：关闭时口令直接通过，开启时必须有本机模板且相似度达标；开关通过内部 RAM NVS 任务保存，BOOT 手动录音始终绕过验证。

**Tech Stack:** ESP-IDF 5.5.4、ESP32-S3/PSRAM、ESP-SR MultiNet 7、C++20 主机单测、NVS、FastAPI/Pydantic、本机 HTML 控制台。

---

### Task 1: 建立模块行为测试

**Files:**
- Create: `firmware/esp32_voice_idf/tests/test_speaker_verification.cpp`
- Create: `firmware/esp32_voice_idf/tests/verify_speaker_verification_integration.py`
- Modify: `gateway/tests/test_console_routes.py`

1. 写测试，规定关闭时直接允许、开启且模板缺失时拒绝、匹配时接受、静音时拒绝。
2. 写集成策略测试，规定 MultiNet 命中后按持久化开关选择直接应答或本地验证，BOOT 不验证。
3. 写 Gateway 测试，规定 `speaker_verification_settings` 只接受布尔 `enabled`，控制台必须有快速开关。
4. 运行三个定向测试并确认因功能缺失而失败。

### Task 2: 建立 ESP32-S3 本地说话人验证组件

**Files:**
- Create: `firmware/esp32_voice_idf/components/sesame_voice/include/sesame_voice/speaker_verification.h`
- Create: `firmware/esp32_voice_idf/components/sesame_voice/speaker_verification.cpp`
- Modify: `firmware/esp32_voice_idf/components/sesame_voice/CMakeLists.txt`
- Modify: `firmware/esp32_voice_idf/tests/run_host_tests.sh`

1. 实现固定内存、无动态模型推理的 1.5 秒 PCM 环形缓冲区。
2. 实现 13 维声学特征、模板余弦相似度和显式启用策略。
3. 在主机测试脚本中编译运行真实组件。
4. 运行单测并确认通过。

### Task 3: 加入持久化开关与固件集成

**Files:**
- Modify: `firmware/esp32_voice_idf/components/sesame_transport/include/sesame_transport/device_config.h`
- Modify: `firmware/esp32_voice_idf/components/sesame_transport/device_config.cpp`
- Create: `firmware/esp32_voice_idf/components/sesame_voice/include/sesame_voice/speaker_verification_store.h`
- Create: `firmware/esp32_voice_idf/components/sesame_voice/speaker_verification_store.cpp`
- Modify: `firmware/esp32_voice_idf/components/sesame_voice/include/sesame_voice/voice_controller.h`
- Modify: `firmware/esp32_voice_idf/components/sesame_voice/voice_controller.cpp`

1. 新增默认 `false` 的 NVS 布尔设置。
2. 使用内部 RAM 任务异步写 NVS，避免 PSRAM voice task 在 flash cache 关闭期间崩溃。
3. 在 PSRAM 分配验证器；仅待唤醒状态缓存 PCM。
4. MultiNet 命中后：关闭则直接应答；开启则验证；模板缺失或不匹配则拒绝。
5. 保持 BOOT 手动入口无条件可用。

### Task 4: 加入本机快速开关

**Files:**
- Modify: `gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py`
- Modify: `gateway/apps/voice_gateway/src/sesame_voice_gateway/console.html`
- Modify: `gateway/tests/test_console_routes.py`

1. 加入严格布尔请求 `speaker_verification_settings`。
2. 在控制台加入“说话人验证”开启/关闭按钮。
3. 通过既有 `operator.control` 通道发送到 ESP32，写入 NVS 后立即生效。
4. 运行 Gateway 定向测试。

### Task 5: 更新注册工具、私有模板和文档

**Files:**
- Create: `tools/train_speaker_verification.py`
- Modify: `.gitignore`
- Modify: `README.md`
- Modify: `docs/releases/v1.6.0.md`
- Modify: `docs/plans/2026-08-17-esp32s3-owner-voice-gate.md`

1. 统一采用 `speaker_verification` / “说话人验证”命名。
2. 私有模板与报告继续 Git 忽略，不输出生物特征数据。
3. 用现有本地 WAV 重新生成新格式私有模板。
4. 检查当前项目不再出现禁用词。

### Task 6: 完整验证

1. 运行 `bash firmware/esp32_voice_idf/tests/run_host_tests.sh`。
2. 运行 Gateway 全量测试。
3. 运行 ESP-IDF 5.5.4 `idf.py -B build-p1 build`。
4. 检查生成的 ESP32-S3 镜像、组件链接和剩余分区空间。
5. 真机烧录仍需设备稳定进入下载模式；若 USB 未恢复，明确报告物理阻塞，不声称真机完成。
