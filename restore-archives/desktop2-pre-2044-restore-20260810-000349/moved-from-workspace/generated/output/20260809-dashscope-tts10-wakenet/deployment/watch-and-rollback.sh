#!/usr/bin/env bash
set -u -o pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROLLBACK="$SCRIPT_DIR/rollback.sh"
LOG="$SCRIPT_DIR/verification/rollback-watch.log"
EXPECTED_MAC='28:84:85:a4:ef:1c'
DEADLINE=$(( $(date +%s) + 300 ))
source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh >>"$LOG" 2>&1
printf '%s watching for ESP32-S3 MAC %s\n' "$(date '+%F %T')" "$EXPECTED_MAC" >>"$LOG"
while [[ $(date +%s) -lt $DEADLINE ]]; do
  for port in /dev/cu.usbmodem* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* /dev/cu.usbserial*; do
    [[ -e "$port" ]] || continue
    probe="$(esptool.py --chip esp32s3 --port "$port" --baud 115200 chip_id 2>&1)" || continue
    if grep -qi "MAC: $EXPECTED_MAC" <<<"$probe"; then
      printf '%s matched %s\n%s\n' "$(date '+%F %T')" "$port" "$probe" >>"$LOG"
      SESAME_ESP32_PORT="$port" "$ROLLBACK" >>"$LOG" 2>&1
      rc=$?
      printf '%s rollback_exit_status=%s\n' "$(date '+%F %T')" "$rc" >>"$LOG"
      exit "$rc"
    fi
  done
  sleep 2
done
printf '%s timeout: no matching ESP32 USB serial device appeared\n' "$(date '+%F %T')" >>"$LOG"
exit 124
