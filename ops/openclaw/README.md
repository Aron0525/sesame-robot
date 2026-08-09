# OpenClaw 运行时

本目录固定电脑端 LLM 网关所需的 OpenClaw 版本。

```bash
cd "/Users/mac/Desktop/2/ops/openclaw"
sh install_openclaw.sh
```

脚本还会执行 `openclaw daemon install/start/status`，在 macOS 上注册
`launchd` 服务。完成后，在 `gateway/.env` 配置
`SESAME_OPENCLAW_TOKEN` 与 `SESAME_OPENCLAW_SESSION_KEY_SECRET`，再启动
`gateway`。

检查服务：

```bash
openclaw daemon status
```
