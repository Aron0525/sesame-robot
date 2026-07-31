# 安全与隐私边界

## 信任边界

首版以一位用户或一个家庭为一个信任边界：

```text
一位用户
├─ 一组已配对 ESP32-S3
├─ 一个 Sesame Voice Gateway
├─ 一个 OpenClaw Gateway
├─ 独立凭据
└─ 独立 workspace / sandbox
```

多个互不信任用户不能共享同一个具有工具权限的 OpenClaw Gateway。

## 设备连接

- 每台 ESP32-S3 使用独立 `device_id` 和凭据。
- mDNS 只做发现，`gateway_id`、TLS 身份和配对记录用于确认服务。
- WSS 校验证书或固定公钥。
- JSON 和 binary frame 都限制大小和速率。
- `sequence`、时间戳和会话 nonce 用于重放检查。
- 设备撤销后，Voice Gateway 拒绝其新连接。

## 音频隐私

- 原始 PCM、Opus 默认仅驻留内存。
- 默认不录音、不落盘、不上传云端。
- 日志不记录音频 payload。
- OpenClaw 只接收 ASR 文本。
- ASR/TTS 若切换云端，必须经过显式隐私配置，不能静默回退。

## OpenClaw 沙盒

基线建议：

```json5
{
  agents: {
    defaults: {
      sandbox: {
        mode: "all",
        scope: "session",
        workspaceAccess: "none"
      }
    }
  },
  tools: {
    deny: [
      "exec",
      "process",
      "read",
      "write",
      "edit",
      "apply_patch",
      "browser",
      "gateway",
      "nodes",
      "sessions_spawn",
      "sessions_send",
      "cron"
    ]
  }
}
```

- 禁止 `tools.elevated`。
- 不挂载用户主目录、SSH、浏览器资料、云凭据或 Docker socket。
- 业务确实需要某项工具时，逐项加入白名单。
- 配置变更后执行 OpenClaw security audit。

## 动作安全

```text
OpenClaw：只生成白名单意图
Voice Gateway：校验 Schema、权限、时长和频率
ESP32-S3：校验机械限位、电流、姿态、deadline 和急停
```

三层中的任何一层都可以拒绝动作，后续层不能绕过拒绝。
