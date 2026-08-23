#include "sesame_protocol/turn_state.h"

namespace sesame::protocol {

bool TurnStateMachine::apply(TurnEvent event) {
  if (event == TurnEvent::kConnectionLost || event == TurnEvent::kInterrupted ||
      event == TurnEvent::kFailed) {
    state_ = TurnState::kIdle;
    generation_id_ = 0;
    return true;
  }

  switch (state_) {
    case TurnState::kIdle:
      if (event == TurnEvent::kListenRequested) {
        state_ = TurnState::kListening;
        return true;
      }
      return false;
    case TurnState::kListening:
      if (event == TurnEvent::kEndpointDetected) {
        state_ = TurnState::kThinking;
        return true;
      }
      return false;
    case TurnState::kThinking:
      if (event == TurnEvent::kTtsStarted) {
        state_ = TurnState::kSpeaking;
        return true;
      }
      return false;
    case TurnState::kSpeaking:
      if (event == TurnEvent::kTtsStopped) {
        state_ = TurnState::kIdle;
        return true;
      }
      if (event == TurnEvent::kListenRequested) {
        state_ = TurnState::kListening;
        return true;
      }
      return false;
  }
  return false;
}

bool TurnStateMachine::start_generation(uint32_t generation_id) {
  if (state_ != TurnState::kThinking || generation_id == 0 ||
      generation_id <= generation_id_) {
    return false;
  }
  generation_id_ = generation_id;
  state_ = TurnState::kSpeaking;
  return true;
}

bool TurnStateMachine::start_test_generation(uint32_t generation_id) {
  if (state_ != TurnState::kIdle || generation_id == 0 ||
      generation_id <= generation_id_) {
    return false;
  }
  generation_id_ = generation_id;
  state_ = TurnState::kSpeaking;
  return true;
}

bool TurnStateMachine::stop_generation(uint32_t generation_id) {
  if (state_ != TurnState::kSpeaking || generation_id != generation_id_) {
    return false;
  }
  state_ = TurnState::kIdle;
  return true;
}

bool TurnStateMachine::is_current_generation(uint32_t generation_id) const {
  return state_ == TurnState::kSpeaking && generation_id != 0 &&
         generation_id == generation_id_;
}

const char* to_string(TurnState state) {
  switch (state) {
    case TurnState::kIdle:
      return "idle";
    case TurnState::kListening:
      return "listening";
    case TurnState::kThinking:
      return "thinking";
    case TurnState::kSpeaking:
      return "speaking";
  }
  return "unknown";
}

}  // namespace sesame::protocol
