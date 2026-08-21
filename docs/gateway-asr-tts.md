# DashScope 云端 ASR/TTS 接入

## 1. 需要申请什么

不需要分别申请两个 API。需要：

1. 开通阿里云百炼 Model Studio；
2. 选择部署地域；
3. 在该地域的 Workspace 中创建一个 API Key；
4. 记录同一 Workspace 的 `Workspace ID`。

同一个地域、同一个 Workspace 的 API Key 用于：

- ASR：`fun-asr-realtime`；
- TTS：`qwen-audio-3.0-tts-flash`；
- 音色注册：`voice-enrollment`。

API Key 和 Workspace ID 必须来自同一地域。创建入口和最新规则以
[阿里云百炼 API Key 文档](https://help.aliyun.com/en/model-studio/get-api-key)
为准。

## 2. 本机配置

```bash
cd "/Users/mac/Desktop/1/SesameV3_语音机器人项目/gateway"
cp .env.example .env
```

在 `.env` 中设置：

```dotenv
SESAME_ASR_PROVIDER=dashscope
SESAME_TTS_PROVIDER=dashscope
# 云端 ASR/TTS 会收到当前轮音频或待合成文本，必须显式同意。
SESAME_ALLOW_REMOTE_SPEECH=true

SESAME_DASHSCOPE_API_KEY=sk-替换为真实值
SESAME_DASHSCOPE_WORKSPACE_ID=替换为真实值
SESAME_DASHSCOPE_REGION=beijing

SESAME_DASHSCOPE_ASR_MODEL=fun-asr-realtime
SESAME_DASHSCOPE_ASR_LANGUAGE=zh
SESAME_DASHSCOPE_TTS_MODEL=qwen-audio-3.0-tts-flash
SESAME_DASHSCOPE_TTS_VOICE_ID=longanhuan_v3.6
SESAME_DASHSCOPE_TIMEOUT_SECONDS=30
```

`beijing` 对应北京地域，`singapore` 对应新加坡地域。默认先使用北京；如果改用新加坡，必须在新加坡地域重新创建 API Key 和 Workspace，并先在控制台确认两个模型均可用。

`.env` 已被 `.gitignore` 排除。不要把真实 Key 写进 `.env.example`、Python、Markdown、终端截图或聊天记录。

## 3. 安装

```bash
uv sync --python 3.12
```

项目固定：

```text
dashscope >= 1.25.17, < 2
```

当前 lock 文件解析到的实际版本以 `uv.lock` 为准。

## 4. 创建克隆音色

参考录音应使用已获得明确授权的真人声音。推荐准备 10–20 秒清晰、连续、无背景音乐、无其他说话人的录音。

音色注册 API 接收可访问的 HTTPS URL，不接受本机 `file://` 路径。先将录音上传到你控制的 HTTPS/OSS 地址，再执行：

```bash
uv run sesame-enroll-voice \
  --audio-url "https://你的地址/reference.wav" \
  --prefix sesame
```

命令只打印返回的 `voice_id`，不会自动修改配置。确认声音正确后，将它写入本机 `.env`：

```dotenv
SESAME_DASHSCOPE_TTS_VOICE_ID=返回的_voice_id
```

音色与创建时指定的 TTS 模型绑定。为 `qwen-audio-3.0-tts-flash` 创建的音色不能直接拿给其他模型使用。

官方参考：

- [实时 ASR](https://help.aliyun.com/en/model-studio/real-time-speech-recognition-user-guide)
- [Qwen-Audio-TTS Python SDK](https://help.aliyun.com/en/model-studio/cosyvoice-tts-python-sdk)
- [音色克隆](https://help.aliyun.com/en/model-studio/voice-cloning-user-guide)

## 5. 启动 Voice Gateway

```bash
uv run uvicorn sesame_voice_gateway.app:app \
  --app-dir apps/voice_gateway/src \
  --host 0.0.0.0 \
  --port 8766
```

健康检查：

```bash
curl -k https://127.0.0.1:8766/healthz
```

启用云端后应看到：

```json
{
  "providers": {
    "asr": "dashscope",
    "tts": "dashscope"
  }
}
```

`/healthz` 只说明配置已加载、进程在运行。云端凭据和完整语音链路须用真实 ESP32 设备联调确认。

## 6. 当前实现边界

- Voice Gateway 仍按“一轮录完再处理”的方式调用 ASR；
- TTS API 虽返回流式 PCM，当前 Pipeline 会先收齐再编码成 Opus 下发；
- API 调用放到工作线程，并有网关级超时；
- OpenClaw 仅对本机传输失败做最多两次的有上限重试；所有重试共享一轮总超时，协议/Schema 错误不重试；
- 用户打断或总超时时，Gateway 会对该 `turn_id` 对应的 OpenClaw `runId` 发送 `chat.abort`；abort 失败不会阻塞设备端 `tts.flush`；
- DashScope 流末尾的累计音频块会被去重，避免重复播放；TTS 仍会先收齐 PCM 再编码下发，尚未实现真正的边生成边播放；
- 仍没有熔断、费用指标和跨进程持久化的请求追踪；
- 原始音频不落盘，但会发送到配置的阿里云地域；
- 参考音色录音的授权、保存期限、撤回和删除仍需产品侧制定规则。
