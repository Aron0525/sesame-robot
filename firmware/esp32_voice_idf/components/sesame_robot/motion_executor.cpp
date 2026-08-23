#include "sesame_robot/motion_executor.h"

namespace sesame::robot {
namespace {

bool time_reached(uint32_t now_ms, uint32_t deadline_ms) {
  return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

}  // namespace

bool MotionExecutor::start(std::span<const MotionStep> plan,
                           uint32_t now_ms) {
  if (output_ == nullptr || plan.empty()) return false;

  if (active_) cancel();
  plan_ = plan;
  step_index_ = 0;
  active_ = true;
  output_->apply_pose(plan_[step_index_].angles);
  next_step_at_ms_ = now_ms + plan_[step_index_].dwell_ms;
  return true;
}

void MotionExecutor::tick(uint32_t now_ms) {
  while (active_ && time_reached(now_ms, next_step_at_ms_)) {
    ++step_index_;
    if (step_index_ >= plan_.size()) {
      cancel();
      return;
    }
    output_->apply_pose(plan_[step_index_].angles);
    next_step_at_ms_ += plan_[step_index_].dwell_ms;
  }
}

void MotionExecutor::cancel(bool release_output) {
  active_ = false;
  plan_ = {};
  step_index_ = 0;
  next_step_at_ms_ = 0;
  if (release_output && output_ != nullptr) output_->release_all();
}

}  // namespace sesame::robot
