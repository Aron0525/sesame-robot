# Voice Gateway 本地后端实现指南

> 日期：2026-07-22
> 技术栈：Python 3.12、FastAPI、asyncio、libopus、流式 ASR/TTS、OpenClaw Adapter
> 状态：设计与代码骨架，尚未运行验证

## 1. 目标链路

```text
ESP32 binary Opus
  → FastAPI WebSocket
  → packet parser
  → Opus decoder
  → PCM 16 kHz mono
  → streaming ASR
  → final transcript
  → OpenClaw
  → streaming reply text
  → sentence chunker
  → streaming TTS PCM
  → resampler
  → Opus encoder
  → FastAPI WebSocket
  → ESP32
```

ASR、TTS、Opus 与 WebSocket 都在 Voice Gateway；OpenClaw 只接收文本并返回文本/tool calls。

## 2. 推荐目录

```text
voice_gateway/
├── app.py
├── config.py
├── schemas/
│   ├── control_events.py
│   └── audio_packet.py
├── routers/
│   └── device_audio.py
├── sessions/
│   ├── registry.py
│   ├── state.py
│   └── voice_session.py
├── audio/
│   ├── opus_codec.py
│   ├── resampler.py
│   ├── vad.py
│   └── sentence_chunker.py
├── providers/
│   ├── asr/base.py
│   ├── asr/funasr.py
│   ├── tts/base.py
│   ├── tts/provider.py
│   └── agent/openclaw.py
├── services/
│   ├── turn_manager.py
│   └── control_adapter.py
└── tests/
```

## 3. 每条连接的任务模型

```text
receive_loop
  ├── binary → rx_opus_queue
  └── JSON   → control_queue

asr_loop
  → decode Opus
  → push PCM to streaming ASR
  → emit asr.partial/final

agent_loop
  → consume asr.final
  → call OpenClaw
  → emit text delta/tool calls

tts_loop
  → sentence chunks
  → streaming TTS PCM
  → resample
  → encode Opus
  → tx_queue

send_loop
  → serialize JSON/binary
  → websocket.send_*
```

所有 queue 必须设置 `maxsize`。禁止无限缓存，也禁止每收到一个 20 ms packet 就创建一个无约束 task。

## 4. 核心类型

```python
from dataclasses import dataclass, field
from enum import StrEnum
import asyncio


class SessionPhase(StrEnum):
    IDLE = "idle"
    LISTENING = "listening"
    THINKING = "thinking"
    SPEAKING = "speaking"
    INTERRUPTING = "interrupting"


@dataclass(slots=True)
class VoiceSessionState:
    session_id: str
    tenant_id: str
    user_id: str
    device_id: str
    turn_id: str | None = None
    generation_id: int = 0
    phase: SessionPhase = SessionPhase.IDLE


@dataclass(slots=True)
class VoiceSessionQueues:
    rx_opus: asyncio.Queue[bytes] = field(
        default_factory=lambda: asyncio.Queue(maxsize=20)
    )
    asr_final: asyncio.Queue[str] = field(
        default_factory=lambda: asyncio.Queue(maxsize=2)
    )
    tts_text: asyncio.Queue[str] = field(
        default_factory=lambda: asyncio.Queue(maxsize=8)
    )
    tx: asyncio.Queue[bytes | str] = field(
        default_factory=lambda: asyncio.Queue(maxsize=30)
    )
```

20 个上行 packet × 20 ms 约为 400 ms 缓冲。达到上限时应中断该 utterance 或丢弃最旧的完整 packet并报告 discontinuity，不能截断 packet 内容。

## 5. Opus codec

MVP 可通过 Python wrapper 调用系统 `libopus`。以下是接口骨架，具体 wrapper API 需要按选定包验证：

```python
from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class AudioFormat:
    sample_rate: int = 16_000
    channels: int = 1
    frame_duration_ms: int = 20

    @property
    def frame_size(self) -> int:
        return self.sample_rate * self.frame_duration_ms // 1000


class OpusCodec:
    def __init__(self, audio_format: AudioFormat) -> None:
        self.audio_format = audio_format
        self.decoder = create_opus_decoder(
            sample_rate=audio_format.sample_rate,
            channels=audio_format.channels,
        )
        self.encoder = create_opus_encoder(
            sample_rate=audio_format.sample_rate,
            channels=audio_format.channels,
            application="voip",
        )

    def decode(self, opus_packet: bytes) -> bytes:
        return self.decoder.decode(
            opus_packet,
            frame_size=self.audio_format.frame_size,
        )

    def encode(self, pcm_s16le: bytes) -> bytes:
        expected_size = self.audio_format.frame_size * 2 * self.audio_format.channels
        if len(pcm_s16le) != expected_size:
            raise ValueError(f"expected {expected_size} PCM bytes")

        return self.encoder.encode(
            pcm_s16le,
            frame_size=self.audio_format.frame_size,
        )
```

20 ms、16 kHz、mono、s16le 的 PCM 固定是 `320 samples × 2 = 640 bytes`。Opus packet 是变长 bytes。

不要让多个 session 并发调用同一个 encoder/decoder 实例；codec state 属于各自 audio stream。

## 6. ASR Provider 接口

```python
from collections.abc import AsyncIterator
from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True, slots=True)
class AsrEvent:
    text: str
    is_final: bool
    confidence: float | None = None


class StreamingAsrSession(Protocol):
    async def push_pcm(self, pcm_s16le: bytes) -> None: ...
    async def events(self) -> AsyncIterator[AsrEvent]: ...
    async def finish_utterance(self) -> None: ...
    async def close(self) -> None: ...


class AsrProvider(Protocol):
    async def open_session(
        self,
        *,
        sample_rate: int,
        language: str,
    ) -> StreamingAsrSession: ...
```

ASR 读取的是解码后的 PCM，不读取 Opus。`partial` 用于字幕；只有 `is_final=True` 的稳定文本进入 OpenClaw。

ASR 可以是：

- 本机 FunASR streaming service；
- 本机其他流式模型；
- 云 ASR WebSocket API。

Voice Gateway 只依赖 Provider 接口，不把供应商 SDK 写进 session orchestration。

## 7. TTS Provider 接口

```python
from collections.abc import AsyncIterator
from typing import Protocol


class TtsProvider(Protocol):
    async def synthesize(
        self,
        *,
        text: str,
        voice: str,
        sample_rate: int,
        cancel_event: asyncio.Event,
    ) -> AsyncIterator[bytes]:
        """Yield PCM s16le chunks, not WAV files."""
        ...
```

优先让 TTS 返回裸 PCM chunks。若供应商只返回 WAV，需要先解析 WAV header；不要把 WAV header 当 PCM 送进 Opus encoder。

TTS sample rate 必须和下行 Opus encoder 一致。若 TTS 输出 24 kHz，而协议协商为 16 kHz，先通过高质量 streaming resampler 转为 16 kHz，再按 640 bytes 切成 20 ms PCM frame 编码。

## 8. OpenClaw Adapter

```python
from collections.abc import AsyncIterator
from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True, slots=True)
class AgentTextDelta:
    text: str


@dataclass(frozen=True, slots=True)
class AgentToolCall:
    name: str
    arguments: dict[str, object]


AgentEvent = AgentTextDelta | AgentToolCall


class AgentProvider(Protocol):
    async def respond(
        self,
        *,
        tenant_id: str,
        user_id: str,
        session_id: str,
        turn_id: str,
        text: str,
        cancel_event: asyncio.Event,
    ) -> AsyncIterator[AgentEvent]: ...
```

OpenClaw Adapter 将 `AgentTextDelta` 送给句子切分器/TTS，将 `AgentToolCall` 送给 Control Adapter。它不接触 PCM 或 Opus。

## 9. WebSocket route

```python
from fastapi import APIRouter, Depends, WebSocket, WebSocketDisconnect

router = APIRouter()


@router.websocket("/device-audio")
async def device_audio_socket(
    websocket: WebSocket,
    identity: DeviceIdentity = Depends(authenticate_device),
) -> None:
    await websocket.accept()

    session = create_voice_session(
        websocket=websocket,
        identity=identity,
    )

    try:
        async with asyncio.TaskGroup() as task_group:
            task_group.create_task(session.receive_loop())
            task_group.create_task(session.asr_loop())
            task_group.create_task(session.agent_loop())
            task_group.create_task(session.tts_loop())
            task_group.create_task(session.send_loop())
    except WebSocketDisconnect:
        pass
    finally:
        await session.close()
```

`authenticate_device` 必须通过 token/证书解析出 tenant/user/device；不能使用客户端 JSON 中的 user_id。

## 10. Receive loop

Starlette/FastAPI 的原始 receive event 可以区分 binary 和 text：

```python
async def receive_loop(self) -> None:
    while True:
        message = await self.websocket.receive()

        if message["type"] == "websocket.disconnect":
            raise WebSocketDisconnect()

        binary_data = message.get("bytes")
        if binary_data is not None:
            packet = parse_audio_packet(binary_data)
            validate_audio_packet(packet, self.state)
            await put_opus_with_backpressure(self.queues.rx_opus, packet.payload)
            continue

        text_data = message.get("text")
        if text_data is not None:
            event = ControlEvent.model_validate_json(text_data)
            await self.handle_control_event(event)
```

协议解析必须校验 version、type、payload length、frame duration、session、sequence 和最大消息尺寸。

## 11. ASR loop

```python
async def asr_loop(self) -> None:
    asr_session = await self.asr_provider.open_session(
        sample_rate=self.audio_format.sample_rate,
        language="zh-CN",
    )

    event_task = asyncio.create_task(self.consume_asr_events(asr_session))

    try:
        while True:
            opus_packet = await self.queues.rx_opus.get()
            try:
                pcm_s16le = await asyncio.to_thread(
                    self.opus_codec.decode,
                    opus_packet,
                )
                await asr_session.push_pcm(pcm_s16le)
            finally:
                self.queues.rx_opus.task_done()
    finally:
        event_task.cancel()
        await asr_session.close()


async def consume_asr_events(self, asr_session: StreamingAsrSession) -> None:
    async for event in asr_session.events():
        await self.emit_asr_event(event)
        if event.is_final and event.text.strip():
            await replace_latest(self.queues.asr_final, event.text.strip())
```

对少量连接，`asyncio.to_thread` 足以做原型。设备量增加后，不要每 20 ms 对每个连接调用一次通用线程池；改为固定 codec worker pool、原生扩展或独立 Rust/Go media service，并保持同一 stream 的 packet 顺序。

## 12. Agent 与 TTS

```python
async def agent_loop(self) -> None:
    while True:
        transcript = await self.queues.asr_final.get()
        try:
            turn = await self.turn_manager.begin_turn(transcript)

            async for event in self.agent_provider.respond(
                tenant_id=self.state.tenant_id,
                user_id=self.state.user_id,
                session_id=self.state.session_id,
                turn_id=turn.turn_id,
                text=transcript,
                cancel_event=turn.cancel_event,
            ):
                if isinstance(event, AgentTextDelta):
                    for sentence in self.sentence_chunker.push(event.text):
                        await self.queues.tts_text.put(sentence)
                    continue

                await self.control_adapter.handle_tool_call(event, turn)
        finally:
            self.queues.asr_final.task_done()
```

```python
async def tts_loop(self) -> None:
    while True:
        sentence = await self.queues.tts_text.get()
        generation_id = self.state.generation_id

        try:
            pcm_buffer = bytearray()
            async for pcm_chunk in self.tts_provider.synthesize(
                text=sentence,
                voice="default",
                sample_rate=self.audio_format.sample_rate,
                cancel_event=self.turn_manager.cancel_event,
            ):
                pcm_buffer.extend(pcm_chunk)

                while len(pcm_buffer) >= self.pcm_frame_bytes:
                    pcm_frame = bytes(pcm_buffer[: self.pcm_frame_bytes])
                    del pcm_buffer[: self.pcm_frame_bytes]

                    opus_packet = await asyncio.to_thread(
                        self.opus_codec.encode,
                        pcm_frame,
                    )
                    binary_message = build_downlink_packet(
                        payload=opus_packet,
                        generation_id=generation_id,
                    )
                    await self.queues.tx.put(binary_message)
        finally:
            self.queues.tts_text.task_done()
```

实际实现需要定义句末 flush：最后不足 20 ms 的 PCM frame 可补零后编码，或由协议明确丢弃尾部静音。

## 13. 打断

收到 `interrupt` 或在允许 barge-in 时检测到用户开口：

```python
async def interrupt_current_turn(self) -> None:
    self.state.phase = SessionPhase.INTERRUPTING
    self.state.generation_id += 1

    await self.turn_manager.cancel_current_turn()
    drain_queue(self.queues.tts_text)
    drain_queue(self.queues.tx, only_old_audio=True)

    await self.send_control_event(
        event_type="tts.flush",
        payload={"generation_id": self.state.generation_id},
    )
    self.state.phase = SessionPhase.LISTENING
```

ESP32 收到 flush 后也要清空 decoder、PCM ring buffer 和 I2S TX DMA；仅服务端停止发送不够，因为旧音频可能已经在 TCP 和设备缓冲中。

## 14. 错误与安全边界

- 单个 binary message 设置严格上限；拒绝错误 payload length。
- Opus decode error 只丢当前 packet，连续错误达到阈值后断开。
- ASR/TTS/OpenClaw 分别设置 timeout 和 circuit breaker。
- 每设备限制连接数、帧率、bitrate、会话时长和 GPU 配额。
- 不记录原始音频、完整对话、token 和 provider key。
- OpenClaw tool call 必须经过 Control Adapter；模型不能直接写 WebSocket。
- 网络断开后立即取消 ASR/OpenClaw/TTS，并向机器人动作层触发 stop watchdog。

## 15. 推荐实施顺序

1. 写 WSS echo + binary packet parser。
2. 接 libopus，使用固定 Opus 样本验证 encode/decode。
3. 接本地 WAV/PCM dump，仅用于开发验证，生产默认关闭。
4. 接 streaming ASR，先输出 partial/final 日志。
5. 接 OpenClaw Adapter，只返回文本。
6. 接 streaming TTS → PCM → Opus 下行。
7. 加 generation/interrupt/flush。
8. 最后接动作工具、安全策略、监控和多用户隔离。

每一步都应使用录制好的 Opus fixture 和假时钟做确定性测试，再做真实 ESP32 端到端测试。
