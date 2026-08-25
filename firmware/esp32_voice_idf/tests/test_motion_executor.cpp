#include <array>
#include <cassert>

#include "sesame_robot/motion_executor.h"

namespace {

class RecordingOutput final : public sesame::robot::MotionOutput {
 public:
  void apply_pose(const std::array<uint8_t, 8>&) override { ++pose_count; }
  void release_all() override { ++release_count; }

  int pose_count{0};
  int release_count{0};
};

constexpr std::array<sesame::robot::MotionStep, 1> kPlan{{
    {{{90, 90, 90, 90, 90, 90, 90, 90}}, 100},
}};

}  // namespace

int main() {
  RecordingOutput handoff_output;
  sesame::robot::MotionExecutor handoff_executor(&handoff_output);
  assert(handoff_executor.start(kPlan, 0));
  handoff_executor.cancel(false);
  assert(!handoff_executor.active());
  assert(handoff_output.pose_count == 1);
  assert(handoff_output.release_count == 0);

  RecordingOutput safety_output;
  sesame::robot::MotionExecutor safety_executor(&safety_output);
  assert(safety_executor.start(kPlan, 0));
  safety_executor.cancel();
  assert(!safety_executor.active());
  assert(safety_output.release_count == 1);
}
