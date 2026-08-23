# Sesame Endpoint Gateway

这是 ESP32-S3 的端侧 WSS 入口与单设备开发控制台，不在 ESP32 中运行 ASR、TTS 或 Agent。

## 统一控制台

控制台由电脑网关提供，而不是由旧 Arduino 网页或静态文件提供。打开网关根路径 `/` 后，页面会自动连接同源 `/v1/console`，不需要填写控制 API 或状态地址。

- 动作：`rest`、`stand`、`wave`、`stop`。
- 表情：`default`、`happy`、`thinking`。
- 手柄：A 站立、B 挥手、Back / Start 急停。
- 调试命令台与按钮走同一条受控通道；未知动作、表情和超时参数会在网关拒绝。
- 页面只显示固件发出的 `status.update` 和 `action.result`，不会生成替代遥测数据。

本版是**单台机器人、本机开发控制台**。生产使用前必须增加操作员认证、TLS、设备选择以及审计存储；不要把未认证的开发控制台暴露到公网。

本地运行需要在网关机器中显式提供设备身份与 token：

```bash
export SESAME_DEVICE_ID='sesame-v3-001'
export SESAME_DEVICE_TOKEN='replace-with-real-device-token'
export SESAME_GATEWAY_ID='sesame-edge'
python -m uvicorn sesame_endpoint_gateway.server:create_runtime_app --factory --host 127.0.0.1 --port 8788
```

浏览器访问 `http://127.0.0.1:8788/`。真实设备的生产连接仍需通过 TLS 终止层提供 `wss://.../v1/device-stream`；不要把设备 token 放进网页。

> 当前 ESP-IDF `RobotAdapter` 尚未接入实际 `RobotDriver`。控制台、网关转发、回执和状态链路已就绪，但在 `RobotDriver` 实现前，固件会拒绝或无法执行物理动作。这不是网页可替代的能力。

## 已实现的入口边界

- 仅接受 `/v1/device-stream` WebSocket。
- 在 `session.hello` 前验证设备 Bearer token。
- 固定协商 `Opus / 16 kHz / mono / 20 ms`。
- 校验 SSM1 二进制音频帧的魔数、版本、方向、长度和 1,500 字节上限。
- 拒绝下行方向帧被伪装成上行 ASR 输入。

## 尚未可配置的业务适配器

ASR、TTS 与 OpenCo/OpenClaw 必须在端侧配置为独立 Provider。它们的 URL、鉴权、模型和隐私策略尚未写入此项目，原因是这些接口没有被提供；服务不会猜测地址、嵌入 API Key，或自动把用户音频上传到云端。

## 本地测试

```bash
./.venv/bin/python -m unittest discover -s tests -v
```

当前测试只使用虚构设备 token，绝不使用真实凭据。
