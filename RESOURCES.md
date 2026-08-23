# 网关与系统边界资源

## Knowledge

- [NGINX 官方文档](https://nginx.org/en/)
  反向代理、负载均衡、缓存和 TCP/UDP 代理的官方能力说明。用于理解基础代理层。
- [AWS：What is Amazon API Gateway?](https://docs.aws.amazon.com/apigateway/latest/developerguide/welcome.html)
  API 网关的职责、架构和典型能力。用于理解“应用前门”和 API 治理。
- [Kubernetes Gateway API：Introduction](https://gateway-api.sigs.k8s.io/docs/introduction/)
  Kubernetes 官方 Gateway API 的定位，以及它与 API Gateway 产品的区别。
- [Kubernetes Gateway API：API Overview](https://gateway-api.sigs.k8s.io/docs/concepts/api-overview/)
  GatewayClass、Gateway、Listener、Route 和 Service 的资源关系及请求路径。
- [Envoy：Architecture overview](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/arch_overview)
  现代 L4/L7 代理的数据面架构。用于继续学习监听器、过滤器、集群和可观测性。
- [IETF RFC 9110：HTTP Semantics](https://datatracker.ietf.org/doc/rfc9110/)
  HTTP 方法、代理语义与状态码的互联网标准。用于准确解释 401、403、502、503、504 等响应。
- [OpenID Connect Core 1.0](https://openid.net/specs/openid-connect-core-1_0.html)
  基于 OAuth 2.0 的身份层标准。用于区分授权、认证、Access Token 与 ID Token。
- [OWASP API Security Top 10](https://owasp.org/API-Security/)
  API 常见安全风险的行业参考。用于理解对象级授权、认证失败、资源消耗和安全配置。
- [OpenTelemetry Signals](https://opentelemetry.io/docs/concepts/signals/)
  Traces、Metrics、Logs 与 Baggage 的官方概念说明。用于网关可观测性。
- [sherpa-onnx 官方文档](https://k2-fsa.github.io/sherpa/onnx/index.html)
  端侧 ASR、TTS、VAD、流式识别与跨平台运行时文档。用于理解本地语音模型的代码和服务边界。
- [FunASR 官方仓库](https://github.com/modelscope/FunASR)
  中文流式 ASR、VAD、标点、热词与服务部署资料。用于比较 ASR Provider 的实现方式。
- [CosyVoice 官方仓库](https://github.com/FunAudioLLM/CosyVoice)
  双向流式 TTS、gRPC/FastAPI 和 NVIDIA 部署示例。用于理解高质量 TTS 服务封装。
- [OpenClaw Gateway Protocol](https://docs.openclaw.ai/gateway/protocol)
  Gateway 的 WebSocket 文本帧、JSON 握手、设备身份、role 和 scope 协议。用于判断 ESP32 是否应直接连接 OpenClaw。
- [OpenClaw Multi-Agent Routing](https://docs.openclaw.ai/multi-agent)
  Agent、workspace、session store 和路由边界。用于建立 Sesame 专用 Agent。
- [OpenClaw Sandboxing](https://docs.openclaw.ai/gateway/sandboxing)
  Docker、SSH 和 OpenShell 后端的隔离范围、网络、挂载、镜像和运行时限制。
- [OpenClaw Sandbox vs Tool Policy vs Elevated](https://docs.openclaw.ai/gateway/sandbox-vs-tool-policy-vs-elevated)
  区分工具执行位置、工具可调用性和宿主机提权逃生口。
- [OpenClaw Security](https://docs.openclaw.ai/gateway/security)
  单一可信操作者模型、多租户限制、Gateway 暴露和安全审计基线。
- [OpenClaw Memory Configuration](https://docs.openclaw.ai/reference/memory-config)
  Session transcript indexing、跨会话召回和本地存储边界。
- [ESP-IDF：FreeRTOS（IDF）](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)
  ESP32-S3 双核 FreeRTOS、任务亲和性、栈和同步原语的官方说明。用于解释 Sesame 的 `xTaskCreatePinnedToCore`、队列和 mutex。
- [ESP-IDF：Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
  ESP-IDF 的 CMake component、依赖声明和构建入口。用于解释 `main/CMakeLists.txt` 与七个 Sesame 组件如何组成固件。
- [FastAPI：WebSockets](https://fastapi.tiangolo.com/advanced/websockets/)
  FastAPI WebSocket 接收、发送和断开处理的官方资料。用于理解 Gateway 的设备长连接生命周期。

## Wisdom (Communities)

- [Kubernetes SIG Network](https://github.com/kubernetes/community/tree/master/sig-network)
  Kubernetes 网络标准和实现讨论。用于验证 Gateway API 的实际边界与演进方向。
- [Envoy Discussions](https://github.com/envoyproxy/envoy/discussions)
  适合阅读真实代理、网关、性能和配置故障案例。

## Gaps

- 后续需要按实际项目技术栈，补充一个具体产品的部署与排障材料。
- AI Gateway 尚缺少跨厂商、稳定且中立的统一规范，应结合实际产品和自建实现分别评估。
