# mDNS 服务发现与版本配对

## 先确认版本

ESP32 和 Voice Gateway 必须使用同一行配置：

| 配置 | ESP 查询的服务 | Gateway TXT `path` | 设备入口 | 默认端口 |
|---|---|---|---|---:|
| 当前部署：0821 | `_sesame-streamgw._tcp.local.` | `/v2/device-stream` | `/v2/device-stream` | 8766 |
| 仓库 v1.6 默认值 | `_sesame-gw._tcp.local.` | `/v1/device-stream` | `/v1/device-stream` | 8765 |

如果 Gateway 广播 `_sesame-gw`，而 ESP 查询 `_sesame-streamgw`，设备会停在 `Gateway discovery ... ESP_ERR_NOT_FOUND`，不会进入 TLS 或 token 鉴权。不要通过修改 token、关闭 TLS 或重写 NVS 来处理这种版本不匹配。

## 当前 0821 服务记录

当前部署发布：

```text
服务类型：_sesame-streamgw._tcp.local.
实例名称：Sesame Streaming Lab Gateway
主机名：sesame-stream-gateway.local.
端口：8766
```

TXT 只包含非秘密信息：

```text
gateway_id=gw_stream_lab
protocol=1
tls=1
path=/v2/device-stream
```

禁止把 token、Wi-Fi 密码、设备密钥或用户信息写入 mDNS TXT。

## 发现与身份校验

ESP32 的连接顺序是：

```text
连接 NVS 中的 Wi-Fi
→ 查询匹配版本的 mDNS PTR
→ 校验 gateway_id / protocol / tls / path
→ 取得 Gateway IPv4 和端口
→ 使用发现到的 IPv4 建立 WSS
→ 使用 sesame-stream-gateway.local 校验 TLS 证书
→ Gateway 校验设备 Bearer token
→ session.hello / session.ready
```

mDNS 只解决地址发现，不证明服务身份。`gateway_id`、TLS 证书、设备 token 和协议校验缺一不可。

## DHCP 与重连

- 默认不要设置 `SESAME_ADVERTISED_IPV4`。普通 DHCP 地址改变后，Gateway 应重新发布 mDNS。
- ESP32 在首次发现失败、WSS 断线或 Gateway 重启后重新查询 mDNS，并以最高 30 秒的退避重试。
- ESP32 的 HTTP 页面同时发布 `http://<device_id>.local/`。它与 ESP 主动连接 Voice Gateway 是两条独立路径。
- 2.4 GHz 与 5 GHz 本身不会阻止通信；只要路由器把两个频段桥接到同一局域网即可。不能仅凭频段不同判断为网络隔离。
- macOS 工具可能因隐私权限不显示 SSID。判断电脑是否联网时，应结合 `ifconfig`、路由、Gateway 监听状态、ESP IP 可达性和会话快照。

## 故障定位

| 现象 | 所在阶段 | 优先检查 |
|---|---|---|
| 8766/8765 没有监听 | Gateway 未运行 | LaunchAgent 标签是否启用、plist、进程日志 |
| `ESP_ERR_NOT_FOUND` | mDNS 发现 | 固件与 Gateway 的服务类型和 `path` 是否成对、Gateway 是否持续运行 |
| 已选择 mDNS，TLS 失败 | TLS | 根 CA、证书 SAN、发现到的主机名 |
| HTTP 401/403 或 WSS 关闭 | 设备鉴权 | `device_id` 和 token 是否一致 |
| WSS connected 但不 ready | 会话握手 | 协议版本、`session.hello` 和 Gateway 事件 |
| `online=true`、`ready` | 连接完成 | 再测试动作、ASR、OpenClaw 和 TTS |

当前电脑的实际恢复记录见[2026-08-21 ESP32 网关恢复记录](validation/2026-08-21-esp32-gateway-recovery.md)。
