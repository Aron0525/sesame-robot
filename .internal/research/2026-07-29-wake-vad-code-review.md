# 唤醒词 + 自动结束测试版代码审查

日期：2026-07-29

## 已修复问题

1. **重复开始会破坏轮次状态**
   - 问题：`kButtonPressed` 同时代表“开始录音”和“结束录音”。在自动录音已经处于 `Listening` 时，重复开始会把状态错误推进到 `Thinking`，但仍发出新的 `listen.start`。
   - 修复：改为单一含义的 `kListenRequested`，只允许 `Idle -> Listening` 及 `Speaking -> Listening`。`Listening` 中的再次请求被拒绝。自动录音期间按下按钮改为手动结束当前轮次。
   - 覆盖：`components/sesame_protocol/test/test_turn_state.cpp`。

2. **停止顺序可能造成已释放对象被访问**
   - 问题：语音任务可能仍在发送 WebSocket 音频或向 WakeNet/VAD 送音频时，`stop()` 先销毁网关和 WakeNet/VAD。
   - 修复：先置 `running_ = false` 并等待语音任务退出，再停止网关和 WakeNet/VAD。

3. **初始化中途失败时资源泄漏**
   - 问题：Opus 或 WakeNet/VAD 已初始化后，配置加载、GPIO、队列或任务创建失败会提前返回。
   - 修复：`VoiceController::start()` 在每个后续失败分支调用清理逻辑，确保关闭已创建资源。

## 已知限制

- 电脑端不能验证麦克风输入、WakeNet 识别率或 VAD 静音边界；这些需要后续实机日志验证。
- 当前模型固定为官方测试词“你好小智”，最终产品词需要单独替换模型。
- 完整 ESP-IDF 构建使用无空格的临时克隆目录，因为当前工作区路径中的空格会触发本机 CMake/链接器的路径解析问题；源码本身已在原工作区完成组件级交叉编译。

## 本次验证

- `bash tests/run_host_tests.sh`：通过。
- `ninja -C build esp-idf/sesame_voice/libsesame_voice.a`：通过。
- ESP-IDF 5.5.4 完整构建：通过，镜像校验哈希有效。
- 未执行烧录。
