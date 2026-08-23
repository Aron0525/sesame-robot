#!/usr/bin/env bash
set -euo pipefail

root='/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF'
backup='/Users/mac/Documents/sesame robot/output/20260808-wakeword-stability-runtime-fix/original'
openocd='/Users/mac/.espressif/tools/openocd-esp32/v0.12.0-esp32-20251215/openocd-esp32/bin/openocd'

restore=(
  "${backup}/zhima_wakeword_config.h:${root}/components/sesame_voice/include/sesame_voice/zhima_wakeword_config.h"
  "${backup}/wake_vad_engine.cpp:${root}/components/sesame_voice/wake_vad_engine.cpp"
  "${backup}/gateway_client.cpp:${root}/components/sesame_transport/gateway_client.cpp"
  "${backup}/verify_wakeword_model.py:${root}/tests/verify_wakeword_model.py"
  "${backup}/run_host_tests.sh:${root}/tests/run_host_tests.sh"
)

if [[ "${1:-}" == '--dry-run' ]]; then
  printf 'Would restore %s files, remove tests/verify_realtime_wifi_policy.py, build, then flash 0x10000.\n' "${#restore[@]}"
  exit 0
fi

for item in "${restore[@]}"; do
  cp -p "${item%%:*}" "${item#*:}"
done
rm -f "${root}/tests/verify_realtime_wifi_policy.py"
source "$HOME/.espressif/frameworks/esp-idf-v5.5.4/export.sh" >/dev/null
idf.py -C "$root" build
"$openocd" -f board/esp32s3-builtin.cfg \
  -c "program_esp {${root}/build/sesame_robot_v3.bin} 0x10000 verify reset exit"
