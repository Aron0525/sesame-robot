# macOS 常驻服务

Gateway 进程退出或 macOS 登录后要自动恢复时，执行：

```bash
cd "/Users/mac/Desktop/1/SesameV3_语音机器人项目"
sh ops/macos/install_voice_gateway_launchd.sh
```

脚本只读取 `gateway/.env`，不会把 token、API Key 或 TLS 私钥写进 plist。
日志位于 `~/Library/Logs/SesameVoiceGateway/`。状态检查：

```bash
launchctl print "gui/$(id -u)/com.sesame.voice-gateway"
curl http://127.0.0.1:8765/healthz
```

卸载：

```bash
launchctl bootout "gui/$(id -u)/com.sesame.voice-gateway"
rm "$HOME/Library/LaunchAgents/com.sesame.voice-gateway.plist"
```
