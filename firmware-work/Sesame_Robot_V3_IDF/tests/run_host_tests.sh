#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build-host-tests"

mkdir -p "${build_dir}"

python3 "${project_dir}/tests/verify_wakeword_model.py"
python3 "${project_dir}/tests/verify_realtime_wifi_policy.py"
python3 "${project_dir}/tests/test_generate_nvs.py"
python3 "${project_dir}/tests/test_speaker_reference_contract.py"
python3 "${project_dir}/tests/test_turn_completion_contract.py"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_protocol/include" \
  "${project_dir}/components/sesame_protocol/test/test_audio_frame.cpp" \
  "${project_dir}/components/sesame_protocol/audio_frame.cpp" \
  -o "${build_dir}/test_audio_frame"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_protocol/include" \
  "${project_dir}/components/sesame_protocol/test/test_turn_state.cpp" \
  "${project_dir}/components/sesame_protocol/turn_state.cpp" \
  -o "${build_dir}/test_turn_state"

"${build_dir}/test_audio_frame"
"${build_dir}/test_turn_state"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_protocol/include" \
  "${project_dir}/components/sesame_protocol/test/audio_frame_probe.cpp" \
  "${project_dir}/components/sesame_protocol/audio_frame.cpp" \
  -o "${build_dir}/audio_frame_probe"

python3 "${project_dir}/tests/test_audio_downlink_contract.py" \
  "${build_dir}/audio_frame_probe"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_protocol/include" \
  "${project_dir}/components/sesame_protocol/test/test_control_event.cpp" \
  "${project_dir}/components/sesame_protocol/control_event.cpp" \
  -o "${build_dir}/test_control_event"

"${build_dir}/test_control_event"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_protocol/include" \
  "${project_dir}/components/sesame_protocol/test/test_sequence_tracker.cpp" \
  -o "${build_dir}/test_sequence_tracker"

"${build_dir}/test_sequence_tracker"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_protocol/include" \
  "${project_dir}/components/sesame_protocol/test/test_accepted_sequence.cpp" \
  -o "${build_dir}/test_accepted_sequence"

"${build_dir}/test_accepted_sequence"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_audio/include" \
  "${project_dir}/components/sesame_audio/test/test_audio_config.cpp" \
  -o "${build_dir}/test_audio_config"

"${build_dir}/test_audio_config"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_audio/test/fakes" \
  -I"${project_dir}/components/sesame_audio/include" \
  "${project_dir}/components/sesame_audio/test/test_audio_hal_lifecycle.cpp" \
  "${project_dir}/components/sesame_audio/audio_hal.cpp" \
  -o "${build_dir}/test_audio_hal_lifecycle"

"${build_dir}/test_audio_hal_lifecycle"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_audio/include" \
  "${project_dir}/components/sesame_audio/test/test_opus_contract.cpp" \
  -o "${build_dir}/test_opus_contract"

"${build_dir}/test_opus_contract"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_transport/include" \
  "${project_dir}/components/sesame_transport/test/test_transport_policy.cpp" \
  "${project_dir}/components/sesame_transport/transport_policy.cpp" \
  -o "${build_dir}/test_transport_policy"

"${build_dir}/test_transport_policy"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_voice/include" \
  -I"${project_dir}/components/sesame_audio/include" \
  "${project_dir}/components/sesame_voice/test/test_recording_button.cpp" \
  "${project_dir}/components/sesame_voice/recording_button.cpp" \
  -o "${build_dir}/test_recording_button"

"${build_dir}/test_recording_button"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_voice/include" \
  "${project_dir}/components/sesame_voice/test/test_voice_turn_detector.cpp" \
  "${project_dir}/components/sesame_voice/voice_turn_detector.cpp" \
  -o "${build_dir}/test_voice_turn_detector"

"${build_dir}/test_voice_turn_detector"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_voice/include" \
  -I"${project_dir}/components/sesame_audio/include" \
  "${project_dir}/components/sesame_voice/test/test_playback_buffer.cpp" \
  -o "${build_dir}/test_playback_buffer"

"${build_dir}/test_playback_buffer"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_voice/include" \
  "${project_dir}/components/sesame_voice/test/test_playback_telemetry.cpp" \
  -o "${build_dir}/test_playback_telemetry"

"${build_dir}/test_playback_telemetry"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_voice/include" \
  "${project_dir}/components/sesame_voice/test/test_playback_turn_binding.cpp" \
  -o "${build_dir}/test_playback_turn_binding"

"${build_dir}/test_playback_turn_binding"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_voice/include" \
  "${project_dir}/components/sesame_voice/test/test_playback_control.cpp" \
  -o "${build_dir}/test_playback_control"

"${build_dir}/test_playback_control"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_robot/include" \
  "${project_dir}/components/sesame_robot/test/test_action_policy.cpp" \
  "${project_dir}/components/sesame_robot/action_policy.cpp" \
  -o "${build_dir}/test_action_policy"

"${build_dir}/test_action_policy"

c++ -std=c++20 -Wall -Wextra -Werror \
  -I"${project_dir}/components/sesame_robot/include" \
  "${project_dir}/components/sesame_robot/test/test_motion_executor.cpp" \
  "${project_dir}/components/sesame_robot/motion_executor.cpp" \
  -o "${build_dir}/test_motion_executor"

"${build_dir}/test_motion_executor"
