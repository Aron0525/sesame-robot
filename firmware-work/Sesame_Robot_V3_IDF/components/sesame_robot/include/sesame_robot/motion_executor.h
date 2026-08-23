#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "sesame_robot/motion_plan.h"

namespace sesame::robot {

class MotionOutput {
 public:
  virtual ~MotionOutput() = default;
  virtual void apply_pose(const std::array<uint8_t, 8>& angles) = 0;
  virtual void release_all() = 0;
};

class MotionExecutor {
 public:
  explicit MotionExecutor(MotionOutput* output) : output_(output) {}

  bool start(std::span<const MotionStep> plan, uint32_t now_ms);
  void tick(uint32_t now_ms);
  void cancel(bool release_output = true);
  bool active() const { return active_; }

 private:
  MotionOutput* output_;
  std::span<const MotionStep> plan_{};
  size_t step_index_{0};
  uint32_t next_step_at_ms_{0};
  bool active_{false};
};

}  // namespace sesame::robot
