#!/usr/bin/env bash
set +e
repo='/Users/mac/Desktop/1_副本/SesameV3_语音机器人项目'
artifact='/Users/mac/Documents/sesame robot/output/servo-angle-pass-through-desktop-1-copy-20260805'
run() {
  echo "$ $*"
  "$@"
  code=$?
  echo "exit=$code"
  return $code
}
run python3 "$artifact/verify_source_contract.py"
run bash "$repo/firmware/esp32_voice_idf/tests/run_host_tests.sh"
echo '$ source ~/.espressif/frameworks/esp-idf-v5.5.4/export.sh && idf.py build'
(
  source "$HOME/.espressif/frameworks/esp-idf-v5.5.4/export.sh" >/dev/null 2>&1
  cd "$repo/firmware/esp32_voice_idf" && idf.py build
)
code=$?
echo "exit=$code"
echo '$ arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/arduino/Sesame_Robot_WiFi_Controller'
arduino-cli compile --fqbn esp32:esp32:esp32s3 --build-path "$artifact/arduino-build-verified" "$repo/firmware/arduino/Sesame_Robot_WiFi_Controller"
code=$?
echo "exit=$code"
