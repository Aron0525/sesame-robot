#include "sesame_robot/motion_plan.h"

namespace sesame::robot {
namespace {

constexpr std::array<uint8_t, 8> kRestPose{90, 90, 90, 90,
                                            90, 90, 90, 90};
// Channel order: R1, R2, L1, L2, R4, R3, L3, L4.
constexpr std::array<uint8_t, 8> kStandPose{135, 45, 45, 135,
                                             30,  150, 30, 150};
constexpr std::array<uint8_t, 8> kWaveRaisedPose{100, 45, 45, 90,
                                                  80,  180, 180, 180};
constexpr std::array<uint8_t, 8> kWaveLoweredPose{100, 45, 45, 90,
                                                   80,  180, 100, 180};

constexpr std::array<MotionStep, 1> kRestPlan{{{kRestPose, 100}}};
constexpr std::array<MotionStep, 1> kStandPlan{{{kStandPose, 100}}};
constexpr std::array<MotionStep, 6> kWavePlan{{
    {kStandPose, 150},
    {kWaveRaisedPose, 200},
    {kWaveLoweredPose, 300},
    {kWaveRaisedPose, 300},
    {kWaveLoweredPose, 300},
    {kStandPose, 150},
}};

}  // namespace

std::span<const MotionStep> motion_plan_for(std::string_view action) {
  if (action == "rest") return kRestPlan;
  if (action == "stand") return kStandPlan;
  if (action == "wave") return kWavePlan;
  return {};
}

}  // namespace sesame::robot
