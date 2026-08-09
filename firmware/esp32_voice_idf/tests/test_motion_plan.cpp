#include <array>
#include <cassert>
#include <cstdint>

#include "sesame_robot/motion_plan.h"

int main() {
  const auto stand = sesame::robot::motion_plan_for("stand");

  assert(stand.size() == 1);
  assert(stand.front().dwell_ms == 100);
  assert((stand.front().angles == std::array<uint8_t, 8>{
                                      135, 45, 45, 135,
                                      30,  150, 30, 150,
                                  }));
}
