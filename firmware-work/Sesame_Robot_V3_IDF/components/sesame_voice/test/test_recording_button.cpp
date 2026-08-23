#include <cassert>

#include "sesame_voice/recording_button.h"

int main() {
  using namespace sesame::voice;

  RecordingButton button(40, 30000);
  assert(button.update(false, 0) == ButtonEvent::kNone);
  assert(button.update(true, 10) == ButtonEvent::kNone);
  assert(button.update(true, 55) == ButtonEvent::kStartRecording);
  assert(button.update(false, 80) == ButtonEvent::kNone);
  assert(button.update(false, 125) == ButtonEvent::kNone);
  assert(button.update(true, 200) == ButtonEvent::kNone);
  assert(button.update(true, 245) == ButtonEvent::kStopRecording);

  button.reset();
  assert(button.update(true, 1000) == ButtonEvent::kNone);
  assert(button.update(true, 1045) == ButtonEvent::kStartRecording);
  assert(button.update(true, 31046) == ButtonEvent::kMaximumDuration);

  button.reset();
  assert(button.update(true, 40000) == ButtonEvent::kNone);
  assert(button.update(true, 40045) == ButtonEvent::kStartRecording);
  button.finish_from_endpoint();
  assert(!button.recording());
  assert(button.update(false, 40060) == ButtonEvent::kNone);
  assert(button.update(false, 40105) == ButtonEvent::kNone);
  assert(button.update(true, 40120) == ButtonEvent::kNone);
  assert(button.update(true, 40165) == ButtonEvent::kStartRecording);
}
