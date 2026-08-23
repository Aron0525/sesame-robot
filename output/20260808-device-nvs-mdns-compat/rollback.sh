#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF"
openocd="/Users/mac/.espressif/tools/openocd-esp32/v0.12.0-esp32-20251215/openocd-esp32/bin/openocd"
files=(
  "components/sesame_transport/device_config.cpp"
  "components/sesame_transport/transport_policy.cpp"
  "components/sesame_transport/include/sesame_transport/transport_policy.h"
  "components/sesame_transport/gateway_client.cpp"
  "components/sesame_transport/test/test_transport_policy.cpp"
)
if [[ "${1:-}" == "--dry-run" ]]; then
  printf 'Would restore source files:\n'
  printf '  %s\n' "${files[@]}"
  printf 'Would run: idf.py build\n'
  printf 'Would verify and flash the restored app at 0x10000 through USB-JTAG.\n'
  exit 0
fi
for file in "${files[@]}"; do
  cp "$script_dir/original/$file" "$project_dir/$file"
done
source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh >/dev/null
(cd "$project_dir" && idf.py build)
"$openocd" -f board/esp32s3-builtin.cfg -c "program_esp {$project_dir/build/sesame_robot_v3.bin} 0x10000 verify reset exit"
