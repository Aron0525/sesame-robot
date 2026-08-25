#include "sesame_voice/capture_session.h"

namespace sesame::voice {

bool CaptureSession::start(CaptureSource source, uint64_t now_ms) {
  if (active() || source == CaptureSource::kNone) return false;
  source_ = source;
  started_ms_ = now_ms;
  return true;
}

bool CaptureSession::stop() {
  if (!active()) return false;
  reset();
  return true;
}

void CaptureSession::reset() {
  source_ = CaptureSource::kNone;
  started_ms_ = 0;
}

bool CaptureSession::expired(uint64_t now_ms,
                             uint32_t maximum_duration_ms) const {
  return active() && now_ms - started_ms_ >= maximum_duration_ms;
}

}  // namespace sesame::voice
