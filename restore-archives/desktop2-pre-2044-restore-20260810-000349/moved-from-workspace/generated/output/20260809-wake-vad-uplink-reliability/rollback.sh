#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ORIGINAL="$SCRIPT_DIR/original"
PORT="${SESAME_ESP32_PORT:-/dev/cu.usbmodem101}"
BAUD="${SESAME_FLASH_BAUD:-115200}"

restore_sources() {
  while IFS= read -r rel; do
    mkdir -p "$(dirname "$PROJECT_ROOT/$rel")"
    cp -p "$ORIGINAL/$rel" "$PROJECT_ROOT/$rel"
  done <<'FILES'
contracts/schemas/control-event.v1.schema.json
firmware/esp32_voice_idf/components/sesame_voice/CMakeLists.txt
firmware/esp32_voice_idf/components/sesame_voice/include/sesame_voice/voice_controller.h
firmware/esp32_voice_idf/components/sesame_voice/voice_controller.cpp
firmware/esp32_voice_idf/components/sesame_voice/wake_vad_engine.cpp
firmware/esp32_voice_idf/tests/run_host_tests.sh
gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py
gateway/apps/voice_gateway/src/sesame_voice_gateway/pipeline.py
gateway/tests/test_gateway_protocol_hardening.py
gateway/tests/test_privacy_pipeline.py
FILES
}

flash_original() {
  source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh
  esptool.py --chip esp32s3 --port "$PORT" --baud "$BAUD" --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x00000000 "$ORIGINAL/deployed-build-p1/bootloader/bootloader.bin" \
    0x00008000 "$ORIGINAL/deployed-build-p1/partition_table/partition-table.bin" \
    0x00010000 "$ORIGINAL/deployed-build-p1/sesame_robot_v3.bin" \
    0x00410000 "$ORIGINAL/deployed-build-p1/srmodels/srmodels.bin"
  esptool.py --chip esp32s3 --port "$PORT" --baud "$BAUD" verify_flash \
    0x00000000 "$ORIGINAL/deployed-build-p1/bootloader/bootloader.bin" \
    0x00008000 "$ORIGINAL/deployed-build-p1/partition_table/partition-table.bin" \
    0x00010000 "$ORIGINAL/deployed-build-p1/sesame_robot_v3.bin" \
    0x00410000 "$ORIGINAL/deployed-build-p1/srmodels/srmodels.bin"
}

if [[ "${1:-}" == "--dry-run" ]]; then
  printf 'Would restore baseline source files from: %s\n' "$ORIGINAL"
  printf 'Would write and verify old ESP32 images on %s at %s baud.\n' "$PORT" "$BAUD"
  exit 0
fi
if [[ $# -ne 0 ]]; then
  echo "Usage: $0 [--dry-run]" >&2
  exit 2
fi

DOMAIN="gui/$(id -u)"
PLIST="$HOME/Library/LaunchAgents/com.sesame.voice-gateway.plist"
launchctl bootout "$DOMAIN/com.sesame.voice-gateway" 2>/dev/null || true
restore_sources
flash_original
launchctl bootstrap "$DOMAIN" "$PLIST"
launchctl kickstart -k "$DOMAIN/com.sesame.voice-gateway"
echo "Rollback complete: baseline sources restored and original ESP32 image verified."
