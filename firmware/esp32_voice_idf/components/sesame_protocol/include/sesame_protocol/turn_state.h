#pragma once

#include <cstdint>

namespace sesame::protocol {

enum class TurnState {
  kIdle,
  kListening,
  kThinking,
  kSpeaking,
};

enum class TurnEvent {
  kButtonPressed,
  kTtsStarted,
  kTtsStopped,
  kDiscarded,
  kInterrupted,
  kConnectionLost,
  kFailed,
};

class TurnStateMachine {
 public:
  TurnState state() const { return state_; }
  bool apply(TurnEvent event);
  bool start_generation(uint32_t generation_id);
  bool stop_generation(uint32_t generation_id);
  bool is_current_generation(uint32_t generation_id) const;
  uint32_t generation_id() const { return generation_id_; }

 private:
  TurnState state_{TurnState::kIdle};
  uint32_t generation_id_{0};
};

const char* to_string(TurnState state);

}  // namespace sesame::protocol
