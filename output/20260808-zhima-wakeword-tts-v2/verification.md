# zhima_wakeword_tts_v2 delivery verification

## Baseline artifact

Command (exit `0`):

```sh
shasum -a 256 /Users/mac/Documents/sesame\ robot/output/20260808-zhima-wakeword-tts-v2/original/source-model.tflite
wc -c /Users/mac/Documents/sesame\ robot/output/20260808-zhima-wakeword-tts-v2/original/source-model.tflite
```

Literal result:

```text
79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065  source-model.tflite
22456 source-model.tflite
```

## Modified project checks

Command (exit `0`):

```sh
cd /Users/mac/Documents/sesame\ robot/firmware-work/Sesame_Robot_V3_IDF
python3 tests/verify_wakeword_model.py
bash tests/run_host_tests.sh
```

Literal output:

```text
wakeword asset verified: bytes=22456 sha256=79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065
```

`run_host_tests.sh` completed with exit `0` and no stdout/stderr.

Final firmware build command (exit `0`):

```sh
source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh >/dev/null
cd /Users/mac/Documents/sesame\ robot/firmware-work/Sesame_Robot_V3_IDF
idf.py build
```

Literal build tail:

```text
Successfully created esp32s3 image.
Generated /Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF/build/sesame_robot_v3.bin
sesame_robot_v3.bin binary size 0x1995a0 bytes. Smallest app partition is 0x300000 bytes. 0x166a60 bytes (47%) free.
Project build complete.
```

`idf.py size` completed with exit `0`: total image size `1676589` bytes; DIRAM used `151635 / 341760` bytes (44.37%).

## Result

The application embeds the supplied 22,456-byte model, runs it through TFLite Micro on 16 kHz PCM (one-second window, 200 ms stride, raw score threshold `0.86`), and emits the existing `WakeVadSignal`. ESP-SR WakeNet is disabled; ESP-SR VAD begins the existing Opus/WSS dialogue turn after a custom-model wake detection.

## Rollback role

Command (exit `0`):

```sh
bash /Users/mac/Documents/sesame\ robot/output/20260808-zhima-wakeword-tts-v2/rollback.sh --dry-run
```

Literal result: it listed restoration of the seven modified source files, removal of the five custom-model source/assets, removal of the two component-manager directories, then `idf.py reconfigure` as the final apply step.

## Built artifact

```text
/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF/build/sesame_robot_v3.bin
sha256 bf9277889a6643966b35e6cb3296f857b84ddb3497b37d6a7765a1a13a683509
bytes 1676704
```

## ESP32-S3 device flash verification (2026-08-08)

- Port: `/dev/tty.usbmodem101` (native USB Serial/JTAG); chip readback: ESP32-S3 QFN56 rev 0.2, serial/MAC `28:84:85:a4:ef:1c`.
- Firmware image header targets 4 MiB flash; the connected module reports 16 MiB physical flash.
- Verified ROM-loader writes at 115200 baud with `--no-stub`:

```text
0x00000000 bootloader/bootloader.bin      21504 bytes   Hash of data verified.
0x00008000 partition_table/partition-table.bin 3072 bytes Hash of data verified.
0x00010000 sesame-robot-v3.bin          1677312 bytes Hash of data verified.
0x00310000 srmodels/srmodels.bin         414720 bytes Hash of data verified.
```

- Post-reset serial log confirms the runtime loaded the embedded wake-word asset:

```text
wake_vad: custom model ready: zhima_wakeword_tts_v2_int8, 22456 bytes, threshold=0.86, arena=98304 bytes
wake_vad: custom TFLite wake word + VAD ready; feed frame=512 samples
```

- Current runtime constraint: NVS has no per-device configuration (`ESP_ERR_NVS_NOT_FOUND`), so the voice dialogue transport does not start yet. The model and VAD initialization completed before that configuration check.

## Follow-up: existing device NVS compatibility

The earlier `ESP_ERR_NVS_NOT_FOUND` runtime result was caused by treating the intentionally absent `gateway_url` entry as mandatory. A readback confirmed valid prior NVS Wi-Fi and device entries. The corrected application accepts the empty URL, discovers the desktop gateway over mDNS, and forms the `.local` WSS URI. The post-correction ESP32-to-gateway connection is verified in:

`/Users/mac/Documents/sesame robot/output/20260808-device-nvs-mdns-compat/verification.md`
