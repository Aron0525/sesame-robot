#include <cassert>
#include <cstdint>

#include "sesame_voice/capture_session.h"

using sesame::voice::CaptureSession;
using sesame::voice::CaptureSource;

int main() {
  CaptureSession session;

  assert(!session.active());
  assert(session.source() == CaptureSource::kNone);
  assert(!session.start(CaptureSource::kNone, 100));

  assert(session.start(CaptureSource::kManual, 100));
  assert(session.active());
  assert(session.source() == CaptureSource::kManual);
  assert(session.started_ms() == 100);
  assert(!session.start(CaptureSource::kWakeword, 200));
  assert(!session.expired(10099, 10000));
  assert(session.expired(10100, 10000));

  assert(session.stop());
  assert(!session.active());
  assert(!session.stop());

  assert(session.start(CaptureSource::kWakeword, 500));
  assert(session.source() == CaptureSource::kWakeword);
  session.reset();
  assert(!session.active());
  assert(session.source() == CaptureSource::kNone);

  return 0;
}
