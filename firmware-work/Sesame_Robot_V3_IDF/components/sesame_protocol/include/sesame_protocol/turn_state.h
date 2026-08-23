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
  kListenRequested,
  kEndpointDetected,
  kTtsStarted,
  kTtsStopped,
  kInterrupted,
  kConnectionLost,
  kFailed,
};

class TurnStateMachine {
 public:
  TurnState state() const { return state_; }
  bool apply(TurnEvent event);
  bool start_generation(uint32_t generation_id);
  // A local PCM diagnostic is created by the authenticated gateway and has no
  // preceding microphone turn. It may begin only from idle and still obeys
  // the normal monotonically increasing generation rule.
  bool start_test_generation(uint32_t generation_id);
  bool stop_generation(uint32_t generation_id);
  bool is_current_generation(uint32_t generation_id) const;
  uint32_t generation_id() const { return generation_id_; }

 private:
  TurnState state_{TurnState::kIdle};
  uint32_t generation_id_{0};
};

const char* to_string(TurnState state);

}  // namespace sesame::protocol
