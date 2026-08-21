# mDNS 服务发现与身份固定

## mDNS 解决什么

电脑局域网 IP 可能因 DHCP、切换 Wi‑Fi 或睡眠恢复而改变。Voice Gateway 发布 mDNS 服务后，ESP32-S3 通过固定服务类型找到**当前**主机名、IP 和端口：

```text
_sesame-streamgw._tcp.local.
```

mDNS 返回 SRV、A/AAAA 和 TXT 记录，但不证明该服务属于正确用户。

## 服务记录

```text
服务类型：_sesame-streamgw._tcp.local.
实例名称：Sesame Streaming Lab Gateway
端口：8766
```

TXT 只放非秘密信息：

```text
gateway_id=gw_stream_lab
protocol=1
tls=1
path=/v2/device-stream
```

禁止放入 token、WiFi 密码、设备密钥或用户信息。

## 身份固定

ESP32-S3 建立连接前后必须校验：

```text
mDNS 发现地址
+ gateway_id 匹配配对记录
+ TLS 证书或公钥匹配
+ Voice Gateway 验证设备凭据
+ 协议版本兼容
```

当前 Streaming Lab 的 `gateway_id` 固定为 `gw_stream_lab`；实例名称可以重复，`gateway_id` 不可重复。

固件将 mDNS 返回的裸主机名规范成 `<hostname>.local` 后再建立
`wss://<hostname>.local:<port>/v2/device-stream`。TLS 证书的 DNS SAN 必须包含这个 `.local` 名称；不要为绕过 DHCP 或证书问题关闭主机名校验。

## DHCP 与地址刷新

- 默认不要设置 `SESAME_ADVERTISED_IPV4`。网关每 30 秒重新检查当前 LAN IPv4，地址变化时用同一个 mDNS 服务名更新记录。
- `SESAME_ADVERTISED_IPV4` 只适用于路由器已做 DHCP reservation（DHCP 地址保留）或真正静态 IP 的主机。对普通 DHCP 地址强行设置它会让 mDNS 广播旧地址。
- ESP32 在 WSS 断线、首次没有找到服务、连接超时后，以 0.5 秒、1 秒、2 秒……最高 30 秒的退避重新连接 Wi‑Fi、重新发现 mDNS，再新建 WSS。它不缓存旧 IP/旧端口作为永久地址。
- ESP32 自己的 DHCP 地址不影响它主动连 Gateway；网页控制除 `http://<ESP32-IP>/` 外，还发布 `http://<device_id>.local/`（设备 ID 中 `_` 规范为 `-`）。路由器为 ESP32 做 DHCP reservation 仍可作为不支持 mDNS 的网络的备用方案。

## 失败与重连

- 找到多个实例：只选择配对记录中的 `gateway_id`。
- IP 或服务端口变化：重新解析 mDNS，不缓存旧 IP/端口为永久地址。
- 服务消失：指数退避后重新发现。
- 证书或公钥不匹配：拒绝连接，不自动信任新身份。
- 协议版本不兼容：返回明确错误并停止音频上传。
- 访客 WiFi 或客户端隔离：mDNS 和直连可能失败，首版不绕过该限制。

## 适用范围

mDNS 只适合本地链路：

```text
ESP32-S3 → Voice Gateway
```

以下连接使用固定本机配置，不使用 mDNS：

```text
Voice Gateway → OpenClaw：127.0.0.1:18789
Voice Gateway → ASR/TTS：DashScope HTTPS/WSS 配置（不使用 mDNS）
```
