#pragma once

namespace sesame::voice {

enum class GatewayConnectionPhase {
  kDisconnected,
  kConnectingTransport,
  kAwaitingSessionReady,
  kReady,
};

// A transport connection remains in the handshake phase until session.ready
// arrives or the WebSocket implementation explicitly reports a disconnect.
class GatewayConnectionState final {
 public:
  GatewayConnectionPhase phase() const { return phase_; }

  bool should_start() const {
    return phase_ == GatewayConnectionPhase::kDisconnected;
  }

  bool awaiting_connection() const {
    return phase_ == GatewayConnectionPhase::kConnectingTransport ||
           phase_ == GatewayConnectionPhase::kAwaitingSessionReady;
  }

  void start_attempt() { phase_ = GatewayConnectionPhase::kConnectingTransport; }
  void transport_connected() {
    phase_ = GatewayConnectionPhase::kAwaitingSessionReady;
  }
  void session_ready() { phase_ = GatewayConnectionPhase::kReady; }
  void disconnected() { phase_ = GatewayConnectionPhase::kDisconnected; }

 private:
  GatewayConnectionPhase phase_{GatewayConnectionPhase::kDisconnected};
};

}  // namespace sesame::voice
