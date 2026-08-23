#include <cassert>

#include "sesame_protocol/sequence_tracker.h"

int main() {
  sesame::protocol::SequenceTracker tracker;
  assert(!tracker.accept(10, 4, 0));

  tracker.reset(10, 4);
  assert(tracker.accept(10, 4, 0));
  assert(tracker.accept(10, 4, 1));
  assert(!tracker.accept(10, 4, 3));
  assert(!tracker.accept(10, 3, 2));
  assert(!tracker.accept(11, 4, 2));
  assert(tracker.expected_sequence() == 2);

  tracker.reset(10, 5);
  assert(tracker.accept(10, 5, 0));
  tracker.clear();
  assert(!tracker.accept(10, 5, 1));
  return 0;
}
