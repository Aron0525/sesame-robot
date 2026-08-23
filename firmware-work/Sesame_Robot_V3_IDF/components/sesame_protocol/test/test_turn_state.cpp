#include <cassert>

#include "sesame_protocol/turn_state.h"

using sesame::protocol::TurnEvent;
using sesame::protocol::TurnState;
using sesame::protocol::TurnStateMachine;

int main() {
  TurnStateMachine machine;
  assert(machine.state() == TurnState::kIdle);
  assert(machine.apply(TurnEvent::kListenRequested));
  assert(machine.state() == TurnState::kListening);
  assert(!machine.apply(TurnEvent::kListenRequested));
  assert(machine.state() == TurnState::kListening);
  assert(machine.apply(TurnEvent::kEndpointDetected));
  assert(machine.state() == TurnState::kThinking);
  assert(machine.start_generation(7));
  assert(machine.state() == TurnState::kSpeaking);
  assert(machine.is_current_generation(7));
  assert(!machine.is_current_generation(6));
  assert(!machine.stop_generation(6));
  assert(machine.stop_generation(7));
  assert(machine.state() == TurnState::kIdle);

  assert(!machine.apply(TurnEvent::kTtsStarted));
  assert(machine.state() == TurnState::kIdle);
  assert(!machine.apply(TurnEvent::kEndpointDetected));

  assert(machine.apply(TurnEvent::kListenRequested));
  assert(machine.apply(TurnEvent::kConnectionLost));
  assert(machine.state() == TurnState::kIdle);
  assert(machine.generation_id() == 0);

  // Gateway-local PCM diagnostics do not have an upstream listen turn, but
  // must still use a monotonically increasing, authenticated TTS generation.
  assert(!machine.start_generation(1));
  assert(machine.start_test_generation(1));
  assert(machine.state() == TurnState::kSpeaking);
  assert(machine.is_current_generation(1));
  assert(machine.stop_generation(1));
  assert(machine.state() == TurnState::kIdle);
  assert(!machine.start_test_generation(1));
  return 0;
}
