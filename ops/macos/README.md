# macOS 常驻服务

Gateway 进程退出或 macOS 登录后要自动恢复时，执行：

```bash
cd "/Users/mac/Desktop/2"
sh ops/macos/install_voice_gateway_launchd.sh
```

脚本只读取 `gateway/.env`，不会把 token、API Key 或 TLS 私钥写进 plist。
日志位于 `~/Library/Logs/SesameStreamingLabGateway/`。状态检查：

```bash
launchctl print "gui/$(id -u)/com.sesame.streaming-lab-gateway"
curl -ksS https://127.0.0.1:8766/healthz
```

该脚本安装当前检出代码对应的 Gateway。烧录前必须先核对固件与 Gateway 的
mDNS 服务名、端口和 WebSocket path；当前实体机使用 0821 组合，详见
[`docs/run-gateway.md`](../../docs/run-gateway.md)。不要把仓库 v1.6 默认网关与
0821 固件混合运行。

卸载：

```bash
launchctl bootout "gui/$(id -u)/com.sesame.streaming-lab-gateway"
rm "$HOME/Library/LaunchAgents/com.sesame.streaming-lab-gateway.plist"
```
