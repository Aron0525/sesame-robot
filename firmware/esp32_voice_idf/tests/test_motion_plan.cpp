#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_robot/motion_plan.h"

int main() {
  const auto rest = sesame::robot::motion_plan_for("rest");
  assert(rest.size() == 1);
  assert(rest.front().dwell_ms == 100);
  assert((rest.front().angles == std::array<uint8_t, 8>{
                                     90, 90, 90, 90,
                                     90, 60, 90, 60,
                                 }));

  const auto stand = sesame::robot::motion_plan_for("stand");
  assert(stand.size() == 1);
  assert(stand.front().dwell_ms == 100);
  assert((stand.front().angles == std::array<uint8_t, 8>{
                                      135, 45, 45, 135,
                                      30,  120, 30, 120,
                                  }));
}
