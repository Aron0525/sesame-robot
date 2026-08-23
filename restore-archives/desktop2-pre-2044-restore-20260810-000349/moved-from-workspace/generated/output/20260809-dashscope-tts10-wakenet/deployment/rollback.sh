#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
ORIGINAL="$SCRIPT_DIR/original"
PORT="${SESAME_ESP32_PORT:-/dev/cu.usbmodem101}"
BAUD="${SESAME_FLASH_BAUD:-115200}"
restore_source() {
  while IFS= read -r rel; do
    mkdir -p "$(dirname "$PROJECT_ROOT/$rel")"
    cp -p "$ORIGINAL/source/$rel" "$PROJECT_ROOT/$rel"
  done <<'FILES'
firmware/esp32_voice_idf/components/sesame_voice/models/zhima_wakeword_tts_v2_int8.tflite
firmware/esp32_voice_idf/components/sesame_voice/include/sesame_voice/zhima_wakeword_config.h
firmware/esp32_voice_idf/components/sesame_voice/include/sesame_voice/zhima_wakeword_model_data.h
firmware/esp32_voice_idf/components/sesame_voice/zhima_wakeword_model_data.cpp
firmware/esp32_voice_idf/tests/verify_nihao_zhima_wakeword.py
FILES
}
flash_original() {
  source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh
  esptool.py --chip esp32s3 --port "$PORT" --baud "$BAUD" --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x00000000 "$ORIGINAL/flash-images/bootloader/bootloader.bin" \
    0x00008000 "$ORIGINAL/flash-images/partition_table/partition-table.bin" \
    0x00010000 "$ORIGINAL/flash-images/sesame_robot_v3.bin" \
    0x00410000 "$ORIGINAL/flash-images/srmodels/srmodels.bin"
  esptool.py --chip esp32s3 --port "$PORT" --baud "$BAUD" verify_flash \
    0x00000000 "$ORIGINAL/flash-images/bootloader/bootloader.bin" \
    0x00008000 "$ORIGINAL/flash-images/partition_table/partition-table.bin" \
    0x00010000 "$ORIGINAL/flash-images/sesame_robot_v3.bin" \
    0x00410000 "$ORIGINAL/flash-images/srmodels/srmodels.bin"
}
if [[ "${1:-}" == "--dry-run" ]]; then
  printf 'Would restore five baseline model asset/source files from: %s\n' "$ORIGINAL/source"
  printf 'Would write and verify the four original ESP32 partitions on %s at %s baud.\n' "$PORT" "$BAUD"
  exit 0
fi
if [[ $# -ne 0 ]]; then echo "Usage: $0 [--dry-run]" >&2; exit 2; fi
restore_source
flash_original
echo 'Rollback complete: original WakeNet source assets and flash image restored.'
