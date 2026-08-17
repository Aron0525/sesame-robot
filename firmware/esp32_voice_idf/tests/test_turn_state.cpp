#include <cassert>

#include "sesame_protocol/turn_state.h"

int main() {
  sesame::protocol::TurnStateMachine turn;

  assert(turn.apply(sesame::protocol::TurnEvent::kButtonPressed));
  assert(turn.state() == sesame::protocol::TurnState::kListening);
  assert(turn.apply(sesame::protocol::TurnEvent::kButtonPressed));
  assert(turn.state() == sesame::protocol::TurnState::kThinking);

  // A gateway silent-discard is terminal for this turn: it must not create a
  // TTS generation or a follow-up listening window.
  assert(turn.apply(sesame::protocol::TurnEvent::kDiscarded));
  assert(turn.state() == sesame::protocol::TurnState::kIdle);
  assert(turn.generation_id() == 0);
}
