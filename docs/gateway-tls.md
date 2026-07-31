# ESP32 与网关的 TLS 要求

语音固件只会连接 mDNS 服务记录中声明 `tls=1` 的网关，并固定以 `wss://` 建立 WebSocket。开发时不能把普通 `ws://` 网关当作实体设备的替代。

1. 为局域网网关签发证书，证书 DNS SAN 必须包含 mDNS 主机名。
2. 将根证书 PEM 路径写入设备 NVS 配置的 `root_ca_path`，不写进 C++ 源码。
3. 在 `gateway/.env` 启用 `SESAME_TLS_ENABLED=true`，并配置证书与私钥路径。
4. 网关 mDNS TXT 必须包含正确的 `gateway_id`、`protocol=1`、`tls=1` 和 `/v1/device-stream`。

私钥文件只能由当前用户读取；设备 token 不放入 URL、mDNS TXT、日志或 Git。
