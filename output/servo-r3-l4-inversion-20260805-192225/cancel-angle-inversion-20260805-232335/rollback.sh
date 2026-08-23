#!/bin/zsh
set -euo pipefail
ROOT='/Users/mac/Desktop/1_副本/SesameV3_语音机器人项目'
HERE="${0:A:h}"
cp "$HERE/originals/servo_calibration.h" "$ROOT/firmware/esp32_voice_idf/components/sesame_robot/include/sesame_robot/servo_calibration.h"
cp "$HERE/originals/test_servo_calibration.cpp" "$ROOT/firmware/esp32_voice_idf/tests/test_servo_calibration.cpp"
cp "$HERE/originals/Sesame_Robot_WiFi_Controller.ino" "$ROOT/firmware/arduino/Sesame_Robot_WiFi_Controller/Sesame_Robot_WiFi_Controller.ino"
cp "$HERE/originals/legacy_motion_runner.cpp" "$ROOT/firmware/esp32_voice_idf/components/sesame_web/legacy_motion_runner.cpp"
cd "$ROOT/firmware/esp32_voice_idf"
bash tests/run_host_tests.sh
printf 'Rollback source restored and host tests passed. Reflash with: %s\n' "$ROOT/tools/flash_sesame_robot.command"
