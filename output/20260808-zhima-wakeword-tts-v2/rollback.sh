#!/usr/bin/env bash
set -euo pipefail

MODE="${1:---dry-run}"
if [[ "$MODE" != "--dry-run" && "$MODE" != "--apply" ]]; then
  echo "Usage: $0 [--dry-run|--apply]" >&2
  exit 2
fi

PROJECT='/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF'
BACKUP='/Users/mac/Documents/sesame robot/output/20260808-zhima-wakeword-tts-v2/original'
RESTORE=(
  'README.md'
  'main/idf_component.yml'
  'components/sesame_audio/audio_hal.cpp'
  'components/sesame_voice/CMakeLists.txt'
  'components/sesame_voice/include/sesame_voice/wake_vad_engine.h'
  'components/sesame_voice/wake_vad_engine.cpp'
  'components/sesame_voice/voice_controller.cpp'
)
REMOVE=(
  'components/sesame_voice/models/zhima_wakeword_tts_v2_int8.tflite'
  'components/sesame_voice/zhima_wakeword_model_data.cpp'
  'components/sesame_voice/include/sesame_voice/zhima_wakeword_config.h'
  'components/sesame_voice/include/sesame_voice/zhima_wakeword_model_data.h'
  'tests/verify_wakeword_model.py'
)

for rel in "${RESTORE[@]}"; do echo "RESTORE $PROJECT/$rel <- $BACKUP/$rel"; done
for rel in "${REMOVE[@]}"; do echo "REMOVE  $PROJECT/$rel"; done
echo "REMOVE  $PROJECT/managed_components/espressif__esp-tflite-micro"
echo "REMOVE  $PROJECT/managed_components/espressif__esp-nn"
echo "RECONFIGURE $PROJECT with idf.py reconfigure"

if [[ "$MODE" == "--dry-run" ]]; then exit 0; fi

for rel in "${RESTORE[@]}"; do
  test -f "$BACKUP/$rel"
  mkdir -p "$(dirname "$PROJECT/$rel")"
  cp "$BACKUP/$rel" "$PROJECT/$rel"
done
for rel in "${REMOVE[@]}"; do rm -f "$PROJECT/$rel"; done
rm -rf "$PROJECT/managed_components/espressif__esp-tflite-micro" \
       "$PROJECT/managed_components/espressif__esp-nn"

if [[ -z "${IDF_PATH:-}" ]]; then
  source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh >/dev/null
fi
(
  cd "$PROJECT"
  idf.py reconfigure
)
