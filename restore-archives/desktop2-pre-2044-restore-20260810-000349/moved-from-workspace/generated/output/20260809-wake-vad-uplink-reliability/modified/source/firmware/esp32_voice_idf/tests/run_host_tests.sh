#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${TMPDIR:-/tmp}/sesame-v3-host-tests
mkdir -p "$build_dir"

python3 "$project_root/tests/verify_nihao_zhima_wakeword.py"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_voice_turn_detector.cpp" \
  "$project_root/components/sesame_voice/voice_turn_detector.cpp" \
  -o "$build_dir/test_voice_turn_detector"
"$build_dir/test_voice_turn_detector"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_voice_turn_policy.cpp" \
  -o "$build_dir/test_voice_turn_policy"
"$build_dir/test_voice_turn_policy"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_robot/include" \
  "$project_root/tests/test_servo_calibration.cpp" \
  -o "$build_dir/test_servo_calibration"
"$build_dir/test_servo_calibration"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_robot/include" \
  "$project_root/tests/test_motion_plan.cpp" \
  "$project_root/components/sesame_robot/motion_plan.cpp" \
  -o "$build_dir/test_motion_plan"
"$build_dir/test_motion_plan"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_robot/include" \
  "$project_root/tests/test_control_catalog.cpp" \
  "$project_root/components/sesame_robot/action_policy.cpp" \
  -o "$build_dir/test_control_catalog"
"$build_dir/test_control_catalog"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_web/include" \
  -I"$project_root/components/sesame_robot/include" \
  "$project_root/tests/test_web_command.cpp" \
  "$project_root/components/sesame_web/web_command.cpp" \
  -o "$build_dir/test_web_command"
"$build_dir/test_web_command"

node "$project_root/tests/test_web_control_page.cjs"
node "$project_root/tests/test_stand_pose_sources.cjs"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_robot/include" \
  "$project_root/tests/test_robot_adapter_stop_hook.cpp" \
  "$project_root/components/sesame_robot/robot_adapter.cpp" \
  "$project_root/components/sesame_robot/action_policy.cpp" \
  -o "$build_dir/test_robot_adapter_stop_hook"
"$build_dir/test_robot_adapter_stop_hook"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_transport/include" \
  "$project_root/tests/test_transport_policy.cpp" \
  "$project_root/components/sesame_transport/transport_policy.cpp" \
  -o "$build_dir/test_transport_policy"
"$build_dir/test_transport_policy"
