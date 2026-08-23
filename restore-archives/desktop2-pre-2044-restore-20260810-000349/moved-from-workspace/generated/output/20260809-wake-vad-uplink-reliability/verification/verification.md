# Wake / VAD / uplink reliability verification

## Scope executed

1. `no_speech` returns directly to firmware idle; no gateway TTS is produced.
2. VAD initial-speech gate is 10 continuous 20-ms frames (200 ms); endpoint remains 1,000 ms and maximum capture remains 10,000 ms.
3. Opus socket writes run in a bounded uplink worker queue (75 frames / about 1.5 s); the microphone/VAD task only encodes and enqueues.
4. `listen.start` reports its real trigger and `listen.stop` reports its real reason and sent-frame count.

Wi-Fi power-save settings were not changed. OpenClaw action execution remains disabled in `gateway/.env`.

## Baseline and modified images

| role | application SHA-256 |
| --- | --- |
| preserved baseline | `8ba11343efe4f6df1fb9ae8d903cd1a6ba642cc5c04f10633cf4efb262e6a9b1` |
| deployed modified image | `2ef20576bb1cf700d11f7cd7d8b2b78cc94249997dedc12945507086f7f1af1c` |

The exact image offsets and hashes are in `../modified/flash-layout.txt` and `../modified/manifest-sha256.txt`.

## Commands and results

| verification | command record | literal result |
| --- | --- | --- |
| host policy/model tests | `host-tests.log` | `exit_status=0`; custom TFLite model: 22,456 bytes, SHA-256 `79125ad813275425e5ebadd838cf5f81efaac2d158e2bcbe3527286b2fc32065` |
| gateway regression tests | `gateway-tests.log` | `43 passed, 1 warning`; `exit_status=0` |
| physical flash readback | `flash-readback.log` | bootloader, partition table, application, and `srmodels` each `verify OK (digest matched)`; `exit_status=0` |
| gateway/device connection | `gateway-post-verify-snapshot.json` | `dev_001`: `online: true`, stage `ready`, fresh session `ses_69bdaee41e954eb7a2247a605c88f3c6` |
| live idle serial sample | `device-post-verify-reconnect.log` | WakeNet was loaded at threshold `0.85`. A wake candidate at `I (26112)` played the local acknowledgement and entered the 3-second wait; no `listen start` or `listen stop` followed during the captured interval. |

The single pytest collection warning is pre-existing (`TestRecordingStore` has a constructor); it does not fail a test.

## Behavioral checks covered by tests

- `test_pipeline_returns_silent_no_speech_result_without_tts`
- `test_no_speech_turn_returns_to_device_idle_without_tts`
- `test_listen_observability_uses_device_trigger_and_stop_reason`
- `test_voice_turn_policy.cpp` (200 ms gate, 1 s endpoint, 10 s max)

## Rollback

`../rollback.sh --dry-run` validates the rollback operation without touching the board. With no argument it restores the preserved gateway/firmware source files, flashes the four preserved partitions, verifies them by readback, and restarts the gateway.
