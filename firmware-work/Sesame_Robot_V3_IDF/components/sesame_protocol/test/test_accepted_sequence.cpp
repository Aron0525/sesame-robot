#include <cassert>

#include "sesame_protocol/accepted_sequence.h"

int main() {
  sesame::protocol::AcceptedSequence sequence;
  assert(sequence.current() == 0);
  sequence.commit_if(false);
  assert(sequence.current() == 0);
  sequence.commit_if(true);
  assert(sequence.current() == 1);
  sequence.reset();
  assert(sequence.current() == 0);
  return 0;
}
