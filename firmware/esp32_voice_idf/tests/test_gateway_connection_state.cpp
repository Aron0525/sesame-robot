#include <cassert>

#include "sesame_voice/gateway_connection_state.h"

using sesame::voice::GatewayConnectionPhase;
using sesame::voice::GatewayConnectionState;

int main() {
  GatewayConnectionState state;

  assert(state.phase() == GatewayConnectionPhase::kDisconnected);
  assert(state.should_start());

  state.start_attempt();
  assert(state.phase() == GatewayConnectionPhase::kConnectingTransport);
  assert(!state.should_start());

  // A TCP/WSS connection is not yet a usable voice session. The controller
  // must wait for session.ready instead of destroying and recreating it.
  state.transport_connected();
  assert(state.phase() == GatewayConnectionPhase::kAwaitingSessionReady);
  assert(!state.should_start());

  state.session_ready();
  assert(state.phase() == GatewayConnectionPhase::kReady);
  assert(!state.should_start());

  state.disconnected();
  assert(state.phase() == GatewayConnectionPhase::kDisconnected);
  assert(state.should_start());
  return 0;
}
