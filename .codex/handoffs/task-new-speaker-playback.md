# New MAX98357A Playback Integration

- **Task:** Adapt the Sesame V3 firmware's audio playback path to the proven replacement-speaker wiring and method in `/Users/mac/Desktop/test/e`.
- **Objective:** Preserve the existing WSS/SSM1/Opus TTS downlink while making its I2S output compatible with the replacement MAX98357A, and connect OpenClaw reply text to local Gateway TTS.
- **Session:** 2026-08-20.
- **Project:** `/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF`.

## Current State

- **2026-08-22 v1.6.0 / 0821 flash-script repair:** The reported failure was
  reproduced from
  `/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-p1/segmented-flash.log`.
  The v1.6.0 script successfully wrote the application in 16 KiB chunks, then
  retried the entire model image from `0x410000` five times; each attempt lost
  the native USB endpoint after reaching `0x69c000` / 100%, before hash
  verification. GitHub branch `0821` already contains the appropriate hybrid:
  v1.6.0's bounded chunk retry plus model-image chunking, ROM-loader
  `--no-stub`, persistent download mode, re-enumeration recovery, and retry of
  only the failed chunk. That script has now been ported byte-for-byte into the
  dirty local v1.6.0 restoration worktree without bringing over unrelated 0821
  firmware/Gateway changes. A regression test was added at
  `/Users/mac/Desktop/2/tools/test_flash_sesame_robot.py`; it failed against
  the old script, then passed after the repair. `zsh -n`, `--dry-run`, and a
  fresh ESP-IDF build all pass. No hardware flash, NVS write, commit, or push
  was performed; physical acceptance remains pending.

- **2026-08-22 flash diagnosis:** The formal source compiles successfully and
  its `1.8 MiB` application fits the 3 MiB application partition. The project
  uses the matching 4 MiB / DIO / 80 MHz flash settings. The latest failed
  write lost `/dev/cu.usbmodem101` while the ROM loader was erasing or
  reconnecting, before any `Writing at ...` progress; therefore application
  code was not executing and cannot be the direct cause. A project-side
  scripting weakness remains: `tools/flash.sh` hard-resets after `write_flash`
  then immediately opens a separate `verify_flash` session, although this
  board's USB-Serial/JTAG interface re-enumerates. Its large whole-image
  readback also drops after roughly 229 KiB. A later fix should keep the port
  stable across writes and use re-enumeration-aware / chunked verification;
  the immediate blocker is physical USB data/power stability.

- **2026-08-22 current correction — user selected method one (mDNS):** Do not
  restore the old direct-endpoint image described below. The EF:1C NVS image
  `flash-diagnostics/nvs-backups/nvs-current-ef1c-20260821.bin` has no
  `gateway_url` or `gateway_tls`; it contains the required Wi-Fi identity,
  `gw_stream_lab`, token and CA. It was written at `0x9000` with the ESP32-S3
  ROM loader and read back byte-for-byte successfully on 2026-08-22. The
  active Gateway advertises the matching `_sesame-streamgw._tcp.local.` record
  at `192.168.88.98:8766`.
- The previously flashed mDNS app booted on hardware and logged: Wi-Fi IP,
  one mDNS candidate lacking IPv4, then a successful second discovery query
  for `192.168.88.98:8766`, followed by WebSocket start. Gateway snapshot
  confirmed `sesame-stream-lab-001` online and `session.ready`. This proves
  method-one ESP32 → computer-Gateway connectivity.
- The new source now retries mDNS three times and walks the full mDNS address
  linked list, so an AAAA/no-A candidate cannot make discovery return false
  early. `tools/generate_nvs.py` now supports the formal no-`gateway_url`
  provision mode, and its regression test passes.
- Downlink diagnostic result: Gateway successfully sent a 3-second local PCM
  test as 150 Opus/SSM1 WSS packets at 20 ms cadence, but the then-flashed
  firmware correctly—but unhelpfully—rejected the Gateway's `test_...` local
  turn while idle because `TurnStateMachine::start_generation` only accepts
  `thinking`. No `playback.stats` appeared, so the packets did not reach I2S
  and the user's amplifier hiss was not an audio-playback result. Source now
  adds the restricted authenticated `test_` idle path
  `start_test_generation`; its failing host test was added first and all host,
  ESP-IDF, and flash-script tests now pass. The newest application is built
  but **not yet flashed**.
- Latest application flash attempt for that local-PCM fix did not write data:
  esptool lost `/dev/cu.usbmodem101` during/just before erase. Earlier ROM
  loader app writes and short NVS readbacks worked, but the USB-Serial/JTAG
  device is still unstable during app erase/readback. Replug the board using a
  direct, known-good data cable before trying the newest app write. After the
  new image boots, replay the local PCM test and require both
  `tts.playback.buffer` telemetry and audible output; then press BOOT, speak,
  and verify a new `audio.up` plus TTS reply for the upstream path.

- 2026-08-22 later ESP32 reconnect evidence: the board was subsequently
  visible as `/dev/cu.usbmodem101`, and a serial capture reported `Gateway
  discovery or WSS start failed: ESP_ERR_NOT_FOUND`, then scheduled a 30-second
  discovery retry. In the formal firmware this exact branch is reached only
  when NVS `gateway_url` is empty. The existing Gateway is not at fault: a
  local Zeroconf browse found `Sesame Streaming Lab Gateway` at
  `192.168.88.98:8766` under `_sesame-streamgw._tcp.local.`. The board needs
  its prior direct-endpoint NVS recovery image, not a Gateway restart.
- The recovery image
  `flash-diagnostics/nvs-backups/nvs-provisioned-ef1c-20260821.bin` is 24 KiB
  (SHA-256 `241fdb6c…7aae`) and its NVS structure contains `gateway_url` and
  `gateway_tls`. A restore attempt identified the correct ESP32-S3 and MAC
  `28:84:85:a4:ef:1c`, but USB disconnected during the **read-only** new NVS
  backup at 16 KiB of 24 KiB. The shell used `set -e`, so neither NVS nor the
  new application was written. Do not use that partial backup. Restore a
  stable USB data/power connection, then flash this NVS image and the already
  built app before testing WSS and audio again.
- 2026-08-22 ESP32 reconnect diagnosis: Gateway `/healthz` returned 200 and
  its live snapshot identifies `sesame-stream-lab-001` as offline. Gateway
  logs show its last accepted `/v2/device-stream` connections came from the
  expected device IP `192.168.88.183`, so the protocol path and token were
  previously accepted. At diagnosis time the Mac retained the target's ARP
  cache entry (`28:84:85:a4:ef:1c`) but could not route an ICMP packet to it,
  no Espressif USB device/serial node was visible, and a 20-second
  condition-based serial check found none. This proves the current failure is
  upstream of the Gateway: the board is not powered/booted/reachable. No
  Gateway configuration, device NVS, or firmware image was altered while
  investigating. Restore stable board power/data connection first, then use
  serial boot logs to determine why it did not resume Wi-Fi/WSS after reset.
- 2026-08-22 Gateway availability check: the Gateway process (PID 18493) is
  healthy and listening on TCP/8766. Its `/console` endpoint returns HTTP 200
  via loopback and LAN IP. The original hostname also loaded successfully in
  Chrome, which now has an open deliverable tab at
  `https://sesame-stream-gateway.local:8766/console`. Command-line `curl`
  intermittently times out resolving the `.local` name even though macOS's
  system cache has the correct `192.168.88.98` record; a `--resolve` control
  check returned 200, so this is local resolver inconsistency, not a Gateway
  outage. Do not add a permanent `/etc/hosts` mapping without administrator
  credentials; the attempted patch was denied and made no change. The console
  presently shows the ESP32 target as OFFLINE and correctly disables controls,
  matching the unstable USB/reset state; this is distinct from page access.
- 2026-08-22 ESP32 reconnect diagnosis: this is not a Gateway-side WSS,
  certificate, token, or network-address failure. The Mac remains on
  `192.168.88.98/24`, and the Gateway's recent logs prove it previously
  accepted the target's `/v2/device-stream` WSS connection from
  `192.168.88.183` (`28:84:85:a4:ef:1c`) many times. The current observability
  snapshot marks that device offline; ARP is stale and ICMP cannot reach its
  old address, while macOS no longer detects any Espressif USB JTAG/serial
  device. The board is therefore not reaching Wi-Fi or WSS at all. Do not
  restart/reconfigure the healthy Gateway. Restore stable power/data to the
  board, press RST (not BOOT), then capture its boot log before attempting a
  new flash.
- 2026-08-22 compatible flow-control integration: the formal firmware now
  retains the project SSM1/Opus wire format but implements the replacement
  speaker reference's missing feedback half in the current protocol. It has a
  60-packet queue, 30-packet startup reserve, low/high watermarks of 20/40,
  and sends coalesced `playback.stats` reports to the existing Gateway every
  five rendered/downlink packets, at playback start, and on a PCM underflow.
  These reports contain generation ID, actual queue depth, observed high-water
  mark, underflow count, playback state, and the fixed watermarks. The Gateway
  already validates this event and uses it to pause at high water and refill
  below low water, so no Gateway source change was needed.
- The new app image is
  `build/sesame_robot_v3.bin`, SHA-256
  `72411de1ef3a5dd4a74102a8d4c9763ea0bfe2740e8203264fe20a961434e69e`.
  Host tests, ESP-IDF build, flash-script tests, and the Gateway's five
  flow-control tests all pass (details below).
- USB physical verification is currently blocked, not failed at the firmware
  layer: macOS recognizes the connected Espressif USB JTAG/serial device
  (serial `28:84:85:A4:EF:1C`) and intermittently creates
  `/dev/cu.usbmodem101`, but the node disappears before esptool can complete
  even a read-only `chip_id`. No process holds the port. Two flash attempts
  failed while opening the port, before an NVS read or any Flash write, so the
  device has not been changed in this session and audio has not yet been
  physically heard from this image.
- 2026-08-22 playback diagnosis (no source changes yet): the active Gateway is
  the process launched from `/Users/mac/Desktop/2/gateway`, importing its
  Python package from
  `/Users/mac/.config/superpowers/worktrees/sesame-robot/restore-0821/gateway`.
  Its latest successful voice turn synthesized nonempty PCM, encoded Opus, and
  sent a complete paced downlink to the device. Thus the browser → Gateway →
  TTS → WSS network path is healthy.
- The active candidate firmware source in that worktree no longer matches the
  known-working `/Users/mac/Desktop/2/firmware/esp32_voice_idf` playback path:
  it sends speaker I2S1 as 16-bit slots and applies `kSpeakerVolumeLevel = 15`
  (about -13 dB / 22% amplitude), while the reference sends the same mono PCM
  in 32-bit I2S slots with each S16 sample left-shifted by 16 bits and no
  attenuation. The reference and the SPK2 Arduino test use the same physical
  map: GPIO1=BCLK, GPIO2=LRCLK/WS, GPIO3=SDATA.
- The active firmware README claims speaker level 21 (maximum), but the actual
  header has level 15. Its static bus test currently asserts the 16-bit,
  attenuated behavior, so it cannot detect this regression.
- The Web console's `audio.down` trace is Gateway send accounting, not a board
  playback acknowledgement. There are zero `device.playback` events because
  USB serial monitoring is not active, and macOS currently exposes no ESP32
  USB serial device. Therefore the exact flashed app and I2S write outcome
  cannot yet be proven from runtime evidence. Do not flash or alter NVS until
  the board appears as a data serial device and the source/runtime identity is
  checked.
- 2026-08-22 reference correction: `/Users/mac/Desktop/test/e/main/d_main.cpp`
  is the authoritative working reproduction. It uses a dedicated core-1
  playback task, a 60-frame queue with a 30-frame startup reserve, 32-bit
  Philips I2S stereo slots, 8 DMA descriptors of 320 frames, and 0.5 PCM gain
  before left-aligning S16 samples. The historical physical app image recorded
  in this handoff (`3fbdc070…`) is built from
  `firmware-work/Sesame_Robot_V3_IDF`, which already uses the same essential
  32-bit/0.5-gain/60-frame/separate-playback design. The separate `restore-0821`
  worktree does not: it is 16-bit and 16-frame, but it has not been established
  as the image actually on the device. Do not treat that worktree regression as
  the confirmed on-device root cause until serial flash identification is
  available.

- 2026-08-21 flash workflow update: `firmware-work/Sesame_Robot_V3_IDF/tools/flash.sh` now follows the verified stable recovery pattern. It builds, backs up the 24 KiB NVS partition, then writes and verifies only the application at 115200 baud. It preserves bootloader, partition table, and speech model; `--nvs /private/path/device-nvs.bin` deliberately adds a validated 24 KiB NVS write/verify. Do not use bare `idf.py flash` for routine updates because it performs a full image flash at the tool default baud.
- Microphone remains on I2S0: BCLK=14, WS=47, DOUT=48.
- Speaker now uses independent I2S1: BCLK=1, WS/LRC=2, DIN=3.
- The old GPIO1 amplifier-enable control was removed because GPIO1 is now the speaker BCLK.
- Existing voice runtime continues to accept `tts.start` / downlink SSM1 Opus / `tts.stop`; decoded PCM is rendered through the updated `AudioHal`.
- Playback jitter fix: the original formal firmware had an 8-frame queue and consumed it in the voice task. It now uses a 60-frame queue, 30-frame prebuffer (short final utterances exempt), and an independent `sesame_playback` task.
- Formal `endpoint-gateway` now converts a validated OpenClaw `response.reply.text` into local macOS `say` PCM, 20 ms Opus packets, and paced SSM1 downlink frames between `tts.start` and `tts.stop`.
- The Gateway prepares audio before `tts.start`; it sends the first 30 frames immediately and thereafter one frame every 20 ms so it does not overflow the firmware playback queue.
- The currently connected physical target changed from ESP32-S3 MAC `28:84:85:a4:ef:04` to MAC `28:84:85:a4:ef:1c`; after provisioning and reconnect it received DHCP `192.168.88.183`.
- Its original NVS was preserved and backed up, then extended with the explicit endpoint `wss://192.168.88.98:8766/v2/device-stream` plus TLS name `sesame-stream-gateway.local`. This bypasses unreliable LAN mDNS without disabling certificate verification.
- The formal Gateway now accepts both `/v1/device-stream` and the attachment-compatible `/v2/device-stream`, advertises v2, and loads the provisioned `SESAME_DEVICE_TOKENS` map instead of demo-only credentials.
- The conflicting old LaunchAgent `com.sesame.streaming-lab-gateway` is disabled; the formal Gateway is currently running on TLS port 8766.
- The original 40,135-byte `CONTROL & TRACE` browser page is preserved byte-for-byte at the original entry `https://sesame-stream-gateway.local:8766/console`; the newer formal console remains at `/`.
- The formal Gateway now exposes compatibility endpoints for the old page's observability snapshot, SSE snapshot stream, and supported local controls. The formal firmware still intentionally accepts only `rest`, `stand`, `wave`, `stop` plus `default`, `happy`, and `thinking`; legacy servo/settings controls return an explicit unsupported error instead of being falsely acknowledged.
- 2026-08-21 gateway reconnect recovery: the ESP32-S3 `EF:1C` was online on Wi-Fi but its flashed application SHA did not match the current build. It always entered mDNS discovery and then restarted before producing a stable WSS session. The direct-endpoint NVS image was re-applied at `0x9000`; the freshly built application was written only at `0x10000`. Bootloader, partition table, speech model, and storage were not written.

- 2026-08-21 web-control restoration: the formal firmware had constructed `RobotAdapter(nullptr)`, so browser actions reached the Gateway but could not drive the servos. It now starts the original V1.6 OLED, eight-servo driver, `LegacyMotionRunner`, and local HTTP control server before the retained V3 audio/voice runtime. The Gateway forwards the full finite browser catalogue as `operator.control`; actions, manual servo, motion settings, wake-word threshold (persisted in NVS), and stop share the restored device-side path.
- The current app image (SHA-256 `3fbdc070691f9460971c5b8132dbc23e607bff9318c3d7170e09284d4ec631f1`) was written only from `0x10000` through `0x1d6fff`, chunk by chunk with ESP flash hash verification. NVS at `0x9000` was not written.
- The restored `/console` page no longer labels a Gateway HTTP acceptance as an ESP32 confirmation. Gateway action results are stored in the compatibility event stream, and the page shows success only after the device returns `action.result`.
- Gateway process PID `44986` is currently listening on TLS `8766` and was reconstructed from the protected EF:1C NVS backup after a source reload; the ESP32 automatically reconnected as `sesame-stream-lab-001`.
- Physical OLED expressions currently return `control_rejected`, while settings, wake threshold, and stop return completed. This isolates the remaining face-control issue to the OLED/I2C hardware path rather than the browser, Gateway, or ESP32 WSS protocol. Do not claim OLED face output is restored until SDA=8, SCL=9, 3V3/GND, address `0x3C`, and pull-ups are checked on the robot.

## Evidence

- Reference implementation: `/Users/mac/Desktop/test/e/main/d_main.cpp` and `/Users/mac/Desktop/test/e/README.md`.
- Test-first failure: `bash tests/run_host_tests.sh` initially failed because the new pin/port contract did not exist.
- Final verification: `bash tests/run_host_tests.sh` exited 0; `cmake --build build --target app -- -j2` built `build/sesame_robot_v3.bin` successfully, with 47% of the app partition free.
- Root-cause log evidence: `runtime/wss_opus_gateway.log` showed ACK waits averaging 231.1 ms (maximum 404.9 ms) for eight frames that provide only 160 ms of audio; buffer level repeatedly fell to six frames.
- Flash evidence: `bash tools/flash.sh /dev/cu.usbmodem101` completed with verified bootloader, partition, app and model hashes. The device then logged the expected independent I2S map, but could not start its voice runtime because the local NVS `sesame` configuration was absent (`ESP_ERR_NOT_FOUND`).
- Gateway TDD: `endpoint-gateway/tests/test_tts_downlink.py` initially failed because `create_app` had no TTS bridge injection point, then passed after the bridge was implemented.
- Gateway verification: `./.venv/bin/python -m unittest discover -s tests` passed 15 tests. A real local synthesis of “你好，芝麻机器人语音播放测试。” produced 101,760 bytes of 16 kHz PCM and 159 Opus frames using macOS `say`, FFmpeg, and libopus.
- 2026-08-21 hardware recovery: bootloader, current partition table, 1.68 MB application, and 414,016-byte speech model were written successfully in verified short chunks; boot logs showed the new I2S map and `Successfully load srmodels`.
- Root cause chain: formal v1 vs attachment v2 protocol mismatch; demo-only gateway token loading caused HTTP 403; mDNS was not visible reliably from this ESP32; and the first IPv4/TLS adaptation incorrectly passed a full URL as the certificate common name. Each boundary was corrected and retested.
- Final verification: ESP32 `192.168.88.63` established WSS to Gateway `192.168.88.98:8766`; Gateway suite passed 19 tests; a real `POST /v1/openclaw/feedback` returned 200 and forwarded `expression.set`, `tts.start`, audio frames, and `tts.stop` while the device connection remained established.
- 2026-08-22 telemetry TDD: `test_playback_telemetry.cpp` first failed because
  `playback_telemetry.h` did not exist. After the minimal telemetry model and
  protocol event mapping were added, `bash tests/run_host_tests.sh` passed.
  `source /Users/mac/.espressif/frameworks/esp-idf-v5.5.4/export.sh && idf.py
  build` passed and produced the SHA noted above. `python3
  tests/test_flash_script.py` passed (2 tests). The active Gateway source's
  `gateway/tests/test_streaming_downlink.py` passed all 5 flow-control tests
  with test-only dummy provider environment variables; it verifies the exact
  20/40 watermark behaviour and SSM1 downlink contract without making a
  remote request.
- Original-console regression verification: all 20 Gateway tests passed; the restarted TLS service returned HTTP 200 and 40,135 bytes from `/console`; the served SHA-256 exactly matched the original source (`88e74127d8c2020c1e4e840ada91bb0a5c13ec62a09037e5b0229bc594a4d3ac`); snapshot and SSE compatibility endpoints responded successfully.
- 2026-08-21 console-device recovery: the browser-to-Gateway path was healthy but `devices=[]`. Serial evidence showed the previously attached `ef:04` firmware falling back to mDNS and repeatedly returning `ESP_ERR_NOT_FOUND`; USB/ROM inspection then showed the physical target had changed to `ef:1c`. Its own NVS had valid Wi-Fi, device identity, token and CA but lacked `gateway_url`/`gateway_tls`. Backed it up as `flash-diagnostics/nvs-backups/nvs-current-ef1c-20260821.bin`, generated `nvs-provisioned-ef1c-20260821.bin` with the direct TLS endpoint, and flashed only NVS plus the latest app at 115200 baud. Both writes passed hash verification; model and storage partitions were untouched. Gateway then accepted `/v2/device-stream` from `192.168.88.183`, and both snapshot and SSE reported `sesame-stream-lab-001` online.
- 2026-08-21 reconnect verification: gateway process was listening on `192.168.88.98:8766`, its certificate SAN matched `sesame-stream-gateway.local`, and macOS resolved that host to `192.168.88.98`. The ESP32 log proved Wi-Fi association (`192.168.88.183`) but showed `Gateway discovery or WSS start failed: ESP_ERR_NOT_FOUND`; later it also entered an mDNS-triggered reboot loop. Rebuilding produced `build/sesame_robot_v3.bin` (1,678,512 bytes; validation hash `eaf2cc…512377`). The app write retried once after a USB-JTAG re-enumeration and then reached 100%. The Gateway snapshot subsequently reported `sesame-stream-lab-001` online with a live `ses__…` session.

- Full-control build verification: `bash tests/run_host_tests.sh` passed; ESP-IDF `idf.py build` passed with 1,861,936-byte app image (41% app partition free). Gateway test suite passed 23 tests, including the device-result-to-legacy-page regression.
- Real device verification: browser `/console` showed `READY` with the Stand and motion-settings controls enabled. Clicking **保存到机器人** produced the real page message `ESP32 已确认：settings`; the Gateway snapshot recorded a device `action.result` with `status=completed`. Direct WSS checks also returned completed for wake-word threshold and stop.

## Changed Paths

- `/Users/mac/Desktop/2/tools/flash_sesame_robot.command`
- `/Users/mac/Desktop/2/tools/test_flash_sesame_robot.py`
- `components/sesame_audio/include/sesame_audio/audio_contract.h`
- `components/sesame_audio/include/sesame_audio/audio_config.h`
- `components/sesame_audio/include/sesame_audio/audio_hal.h`
- `components/sesame_audio/audio_hal.cpp`
- `components/sesame_audio/test/test_audio_config.cpp`
- `components/sesame_voice/voice_controller.cpp`
- `components/sesame_voice/include/sesame_voice/playback_policy.h`
- `components/sesame_voice/include/sesame_voice/voice_controller.h`
- `components/sesame_voice/test/test_playback_buffer.cpp`
- `README.md`
- `endpoint-gateway/src/sesame_endpoint_gateway/app.py`
- `endpoint-gateway/src/sesame_endpoint_gateway/protocol.py`
- `endpoint-gateway/src/sesame_endpoint_gateway/tts.py`
- `endpoint-gateway/tests/test_tts_downlink.py`
- `endpoint-gateway/tests/test_device_stream.py`
- `endpoint-gateway/tests/test_mdns_discovery.py`
- `endpoint-gateway/tests/test_runtime_config.py`
- `endpoint-gateway/pyproject.toml`
- `endpoint-gateway/README.md`
- `components/sesame_transport/device_config.cpp`
- `components/sesame_transport/gateway_client.cpp`
- `components/sesame_transport/transport_policy.cpp`
- `components/sesame_transport/include/sesame_transport/device_config.h`
- `components/sesame_transport/include/sesame_transport/gateway_client.h`
- `components/sesame_transport/include/sesame_transport/transport_policy.h`
- `firmware-work/Sesame_Robot_V3_IDF/main/app_main.cpp`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_robot/`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_ui/`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_web/`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_controller.cpp`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/include/sesame_voice/playback_telemetry.h`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/test/test_playback_telemetry.cpp`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_protocol/include/sesame_protocol/control_event.h`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_protocol/control_event.cpp`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_protocol/test/test_control_event.cpp`
- `firmware-work/Sesame_Robot_V3_IDF/tests/run_host_tests.sh`
- `firmware-work/Sesame_Robot_V3_IDF/components/sesame_protocol/`
- `endpoint-gateway/src/sesame_endpoint_gateway/static/legacy-console.html`
- `endpoint-gateway/tests/test_legacy_control.py`

## Next Step

2026-08-22 connection repair status: firmware and Gateway compatibility work is
complete, but the current `YuanGuang` WLAN blocks client-to-client unicast. The
ESP32 `EF:1C` is on `192.168.88.183`, discovers the v2 Gateway over mDNS at
`192.168.88.98:8766`, starts WSS, then returns
`ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT` before a TLS handshake. The Mac cannot
reach `192.168.88.183`, the Huawei gateway `192.168.88.1`, or other WLAN clients
either (`No route to host`). macOS firewall is disabled; Local Network access
is enabled for Codex Service and python3.12; disabling Clash TUN did not change
the result and was reverted. Chrome `/console` is open and accurately shows
`OFFLINE`, zero devices.

The next required action is outside the repository: disable Huawei WLAN
AP/client/user isolation for SSID `YuanGuang`, or move both the Mac and ESP32
to a non-isolated hotspot. This is a network-security setting and may require
router credentials, so the user must perform/authorize it. After the LAN allows
unicast, poll `/api/observability/snapshot` until the device is online and
verify Chrome `/console` changes to READY before testing audio or motion.

## 2026-08-24 formal project boundary cleanup

- `/Users/mac/Desktop/2` is now the formal V3 project only. Historical task
  output, restore archives, old Gateway and microphone experiments, local tool
  environments, upstream reference copies, root flash diagnostics, serial logs
  and Desktop metadata were moved—without deletion—to
  `/Users/mac/Documents/sesame robot-backups/desktop-2-20260824/`.
- The current source tree retains the formal `firmware-work/`, `gateway/`,
  `contracts/`, `ops/`, `docs/`, `tools/`, assets and project guidance. A root
  `.gitignore` prevents the moved historical paths from returning to Git.
- `firmware-work/Sesame_Robot_V3_IDF/tools/flash.sh` now stores new NVS backups
  outside the project at `~/Documents/sesame robot-backups/nvs/` by default;
  `SESAME_FLASH_BACKUP_DIR` remains the explicit override. Its regression suite
  passed after the change. Existing NVS backups are in the dated archive, not
  the formal project.
