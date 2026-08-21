#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${TMPDIR:-/tmp}/sesame-v3-host-tests
mkdir -p "$build_dir"

python3 "$project_root/tests/verify_wakenet_removed.py"
python3 "$project_root/tests/verify_multinet_nihao_zhima.py"
python3 "$project_root/tests/verify_nihao_zhima_real19_dataset.py"
python3 "$project_root/tests/verify_manual_button_recording.py"
python3 "$project_root/tests/verify_voice_queue_memory.py"
python3 "$project_root/tests/verify_outbound_transport.py"
python3 "$project_root/tests/verify_downlink_transport.py"
python3 "$project_root/tests/verify_downlink_diagnostics.py"
python3 "$project_root/tests/verify_opus_voice_stack.py"
python3 "$project_root/tests/verify_continuous_conversation.py"
python3 "$project_root/tests/verify_silent_discard.py"
python3 "$project_root/tests/verify_speaker_verification_integration.py"
python3 "$project_root/tests/verify_independent_audio_buses.py"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_audio/include" \
  "$project_root/tests/test_speaker_i2s_config.cpp" \
  -o "$build_dir/test_speaker_i2s_config"
"$build_dir/test_speaker_i2s_config"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_audio/include" \
  "$project_root/tests/test_speaker_volume.cpp" \
  -o "$build_dir/test_speaker_volume"
"$build_dir/test_speaker_volume"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_gateway_connection_state.cpp" \
  -o "$build_dir/test_gateway_connection_state"
"$build_dir/test_gateway_connection_state"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_recording_button.cpp" \
  "$project_root/components/sesame_voice/recording_button.cpp" \
  -o "$build_dir/test_recording_button"
"$build_dir/test_recording_button"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_capture_session.cpp" \
  "$project_root/components/sesame_voice/capture_session.cpp" \
  -o "$build_dir/test_capture_session"
"$build_dir/test_capture_session"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_voice_turn_detector.cpp" \
  "$project_root/components/sesame_voice/voice_turn_detector.cpp" \
  -o "$build_dir/test_voice_turn_detector"
"$build_dir/test_voice_turn_detector"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_wake_capture_policy.cpp" \
  -o "$build_dir/test_wake_capture_policy"
"$build_dir/test_wake_capture_policy"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_wake_threshold_store.cpp" \
  -o "$build_dir/test_wake_threshold_store"
"$build_dir/test_wake_threshold_store"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_protocol/include" \
  "$project_root/tests/test_turn_state.cpp" \
  "$project_root/components/sesame_protocol/turn_state.cpp" \
  -o "$build_dir/test_turn_state"
"$build_dir/test_turn_state"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_audio/include" \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_pcm_preroll_buffer.cpp" \
  "$project_root/components/sesame_voice/pcm_preroll_buffer.cpp" \
  -o "$build_dir/test_pcm_preroll_buffer"
"$build_dir/test_pcm_preroll_buffer"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"$project_root/components/sesame_audio/include" \
  -I"$project_root/components/sesame_voice/include" \
  "$project_root/tests/test_speaker_verification.cpp" \
  "$project_root/components/sesame_voice/speaker_verification.cpp" \
  -o "$build_dir/test_speaker_verification"
"$build_dir/test_speaker_verification"

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
node "$project_root/tests/test_removed_motion_actions.cjs"
node "$project_root/tests/test_stand_pose_sources.cjs"
node "$project_root/tests/test_dance_pose_sources.cjs"
node "$project_root/tests/test_proud_action_sources.cjs"
node "$project_root/tests/test_r3_l4_offset_actions.cjs"
node "$project_root/tests/test_forward_pose_sources.cjs"

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
