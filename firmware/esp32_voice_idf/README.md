# Sesame Robot V3 唯一 ESP-IDF 正式固件

> 当前实体板已刷入 0821 固件，使用 `_sesame-streamgw._tcp.local.`、8766、
> `/v2/device-stream`。本目录当前检出的 v1.6 源码默认使用 `_sesame-gw._tcp.local.`、
> 8765、`/v1/device-stream`。重新编译或烧录前必须同时匹配电脑 Gateway，不能只改一端。

目标芯片：ESP32-S3，16 MB Flash、8 MB OPI PSRAM。

## 固定音频参数

- 设备与电脑之间：Opus VOIP，16 kHz，mono，20 ms/packet。
- ASR/TTS 边界：PCM S16LE，16 kHz，mono。
- I2S 物理总线：16 kHz，32-bit stereo slots。INMP441 使用 left slot，
  MAX98357A 同时接收复制到左右 slot 的 mono PCM。

之所以 I2S 总线使用 32-bit stereo，而网络仍是 mono，是因为 INMP441
在一个 32-bit slot 中输出 24-bit 麦克风样本。麦克风和功放使用独立 I2S
控制器；固件读取麦克风的 left slot 后转换为 mono PCM，播放时将 mono
样本复制到功放的两个 slot。

## 固定引脚

| 信号 | GPIO |
|---|---:|
| INMP441 SCK / I2S0 BCLK | 14 |
| INMP441 WS / I2S0 WS | 47 |
| INMP441 SD | 48 |
| MAX98357A BCLK / I2S1 BCLK | 1 |
| MAX98357A LRC / I2S1 WS | 2 |
| MAX98357A DIN | 3 |
| S0–S7 servo PWM | 4, 5, 6, 7, 10, 11, 12, 13 |
| OLED SDA / SCL | 8 / 9 |

INMP441 的 L/R 接 GND，因此麦克风数据位于 left slot。

## 语音链路

- 不生成 WAV，也不把录音写入 Flash 或文件系统。
- 上行链路为 `I2S PCM → 固定 RAM 帧 → raw Opus → SSM1 → WSS`。
- 下行链路为 `WSS → SSM1 → raw Opus → 固定 RAM 帧 → I2S`。
- BOOT（GPIO0）消抖后作为切换按钮：第一次按下开始录音，第二次按下结束；
  单次录音上限为 30 秒。
- 设备通过 `_sesame-gw._tcp.local.` 查找电脑网关，校验
  `gateway_id/protocol/tls/path`，再使用私有根证书和设备 Bearer token 建立
  WSS 长连接。
- 首次找不到 Gateway、Wi‑Fi 短暂中断、Gateway 重启或 mDNS IP/端口变化时，语音
  任务不会退出：它会以 0.5 秒、1 秒、2 秒……最高 30 秒重新连接 Wi‑Fi、重新发现
  mDNS 并新建 WSS。TLS 始终校验 `<mdns-hostname>.local`，不会为恢复连接关闭证书
  主机名检查。

## 网页控制

- ESP32 连上 NVS 中配置的同一 Wi-Fi 后，原网页控制台在
  `http://<ESP32 的 IPv4 地址>/` 可用；不再启动第二个 Arduino 固件或 AP。
- 同时发布 `http://<device_id>.local/`：设备 ID 中的 `_` 会规范成 `-`，例如
  `sesame_v3_001` 对应 `http://sesame-v3-001.local/`。这比 DHCP IPv4 稳定，多个
  设备时要求 `device_id` 唯一；固件也发布 `_http._tcp.local.`，可由 Bonjour/mDNS
  浏览器发现。
- 该 IPv4 通常由 DHCP 分配，可能变化；语音 WSS 不依赖它。需要固定网页入口时在
  路由器为 ESP32 做 DHCP reservation（DHCP 地址保留）。
- 网页访问要求电脑和 ESP32 在同一非访客 Wi-Fi/VLAN，且网络允许客户端间的 mDNS
  多播和 TCP 单播；访客网络、AP client isolation（客户端隔离）或全局 TUN 路由规则
  可能让网页不可达，即使语音设备仍能主动连到电脑网关。
- 页面保留原有的 19 个姿势／移动动作、8 路舵机角度滑杆和动作参数设置。
- 页面保留原始表情位图和循环、单次、往返动画；`idle` 表情带随机眨眼。
- `GET /api/status` 返回网页动作状态与当前 OLED 表情；
  `POST /api/command` 接受受校验的 `action`、`expression`，或 `servo` / `angle`。
- 网页动作运行期间临时独占舵机 PWM；动作结束后自动归还给语音运行时。
  `STOP ALL` 会立即取消网页动作并释放 S0–S7 输出。

## 舵机动作安全边界

- 来自语音网关／模型的动作仅接受 `rest`、`stand`、`wave` 与 `stop`；不接受模型
  直接给出的单路角度或未列入白名单的动作名称。
- 语音网关／模型可请求的表情仅为 `idle`、`happy`、`sad`、`angry`、
  `surprised`、`sleepy`、`love`、`excited`、`confused`、`thinking`。网页仍可使用
  原项目为本地手动动作准备的全部表情；两类入口不共享“模型可控制”的权限。
- 本地同一 Wi-Fi 的网页控制经过动作白名单和 1–8 路、0–180 度范围校验后，才可
  执行原控制台的动作或调试单路舵机。
- 上电只配置 PWM 并保持八路输出为低，不会自动将舵机移动到任何姿势。
- 动作在独立 FreeRTOS 任务中执行，避免阻塞语音 WebSocket；`stop` 会取消当前
  动作、清空待执行动作并将 S0–S7 PWM 归零。
- 使用独立 5–6 V 舵机电源，并与 ESP32 共地；禁止由 USB 为八路舵机供电。

## 构建

```bash
source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh
idf.py set-target esp32s3
idf.py build
```

## 每台设备的 NVS 配置

Wi-Fi 密码、设备 token 和私有 CA 不写入源码。复制
`provisioning/device-config.example.json` 到仓库外的私密目录，填入当前设备的
真实值，然后生成 24 KiB NVS 镜像：

```bash
source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh
python3 tools/generate_nvs.py /path/to/device-config.json build/device-nvs.bin
esptool.py --chip esp32s3 --port /dev/cu.usbmodem101 write_flash \
  0x9000 build/device-nvs.bin
```

固件本体和 NVS 分开烧录；更新程序不会要求把凭据提交到 Git。量产时应进一步
启用 Flash Encryption（Flash 加密）和 NVS Encryption（NVS 加密）。当前生成
器只避免源码和日志泄密，单独的 `device-nvs.bin` 仍包含敏感信息，权限设为
`0600`，用完后按设备资产流程保管或销毁。

对于已配置本机 Voice Gateway 的单台开发板，可以使用
`tools/provision_from_gateway_env.py` 从网关 `.env` 和根 CA 生成 NVS。它不会把
令牌或 Wi-Fi 密码写入 JSON 源文件；密码默认只从 macOS 钥匙串读取，或以一次性
标准输入提供。输出应位于 `provisioning/private/`，该路径被忽略。

## 单台开发板的本地编译期配置

如果要沿用旧 Arduino 工程的 `local-config.h` 工作方式，而不是写入 NVS，复制
`components/sesame_transport/include/sesame_transport/local-device-config.example.h`
为同目录的 `local-device-config.h`，填入 Wi-Fi、DeviceID、Gateway token 和根
证书后重新编译。真实文件被 Git 忽略，固件会优先使用它；在此模式下会话 ID 只在
本次开机的 RAM 中保留。此方式适合单台开发板调试；多设备部署仍建议使用私有 NVS。

也可从本机 Gateway 的私有 `.env` 和根 CA 生成该文件。Wi-Fi 密码只经标准输入
传入，不写入命令历史：

```bash
read -rs WIFI_PASSWORD
printf '\n'
printf '%s\n' "$WIFI_PASSWORD" | python3 tools/generate_local_device_config.py \
  --gateway-env ../../gateway/.env --device-id dev_001 --wifi-ssid YuanGuang \
  --output components/sesame_transport/include/sesame_transport/local-device-config.h
unset WIFI_PASSWORD
```
