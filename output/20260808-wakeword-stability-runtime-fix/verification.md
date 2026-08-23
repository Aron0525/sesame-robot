# Wake word and link stability verification

## Baseline failure
- Command: `python3 tests/verify_wakeword_model.py` after updating the expected trained-model contract but before source correction.
- Literal result: `AssertionError: missing model contract value: kFeatureMean = -17.381609f` (exit 1).
- Command: `python3 tests/verify_realtime_wifi_policy.py` before the temporary power-policy change.
- Literal result: `ValueError: substring not found` (exit 1).

## Final source validation
- Command: `bash tests/run_host_tests.sh`.
- Literal results: `wakeword asset and inference contract verified: bytes=22456 sha256=79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065`; `Wi-Fi policy verified: STA power saving preserved` (exit 0).
- Build: `. ~/.espressif/frameworks/esp-idf-v5.5.4/export.sh && idf.py build`.
- Literal result: `Project build complete.` and `sesame_robot_v3.bin binary size 0x1996b0 bytes` (exit 0).
- Image: `/Users/mac/Documents/sesame robot/output/20260808-wakeword-stability-runtime-fix/modified/sesame_robot_v3.bin`, SHA-256 `60a1dd725801d1ccb155f5a67fe69dbab737914dc9f07b1d0562a0cb739a2f2b`.

## Hardware validation
- Flash command: `openocd -f board/esp32s3-builtin.cfg -c "program_esp {.../build/sesame_robot_v3.bin} 0x10000 verify reset exit"`.
- Literal result: `** Verify OK **` (exit 0); log: `/Users/mac/Documents/sesame robot/output/20260808-wakeword-stability-runtime-fix/final-flash.log`.
- TLS observability after reset: `dev_001 online=True stage=ready session=True`.
- WSS socket: `192.168.88.21:8765 -> 192.168.88.183:56694 (ESTABLISHED)`.
- Stability command sampled 30 times at 2-second intervals. Literal result: `samples=30 anomalous=0`; record: `/Users/mac/Documents/sesame robot/output/20260808-wakeword-stability-runtime-fix/final-connection-sample.tsv`.
- BOOT verification from gateway events: IDs 287 and earlier show `listen started {'trigger': 'device_button'}`; prior completed voice turns include 134 uplink and 169 downlink packets.

## Rollback
- Script: `/Users/mac/Documents/sesame robot/output/20260808-wakeword-stability-runtime-fix/rollback.sh`.
- Dry-run was syntax-checked with `bash -n` and executed as `rollback.sh --dry-run` (exit 0).
