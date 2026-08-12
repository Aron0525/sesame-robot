#pragma once

#include <cstdint>

namespace sesame::voice {

enum class CaptureSource : uint8_t {
  kNone = 0,
  kManual,
  kWakeword,
  kFollowup,
};

// Single source of truth for microphone capture. Physical BOOT and the local
// wake detector are inputs to this state; neither owns a separate recording
// flag. This keeps repeated BOOT presses and wake-word turns on one lifecycle.
class CaptureSession {
 public:
  bool start(CaptureSource source, uint64_t now_ms);
  bool stop();
  void reset();

  bool active() const { return source_ != CaptureSource::kNone; }
  CaptureSource source() const { return source_; }
  uint64_t started_ms() const { return started_ms_; }
  bool expired(uint64_t now_ms, uint32_t maximum_duration_ms) const;

 private:
  CaptureSource source_{CaptureSource::kNone};
  uint64_t started_ms_{0};
};

}  // namespace sesame::voice
