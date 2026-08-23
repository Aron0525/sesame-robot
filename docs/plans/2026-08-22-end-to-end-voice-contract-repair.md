# Sesame V3 End-to-End Voice Contract Repair Implementation Plan

> **For Codex:** Use `/Users/mac/.codex/skills/executing-plans/SKILL.md` to implement this plan task-by-task.

**Goal:** Make the formal Sesame V3 project internally compatible from ESP32 recording through Gateway/ASR/OpenClaw/search/TTS to confirmed MAX98357 playback, and remove its dependency on an untracked external Gateway worktree.

**Architecture:** Promote the currently running full Voice Gateway into the project as `gateway/` while preserving the legacy `endpoint-gateway/` for migration reference. Treat `contracts/schemas` as the shared wire-contract source; add cross-component regression fixtures so firmware and Gateway cannot each pass while disagreeing. Keep audio transport SSM1/Opus unchanged and repair only turn binding, flow control, cancellation, telemetry, and deployment ownership.

**Tech Stack:** ESP-IDF 5.5.4 / C++20 host tests, Python 3.12 / FastAPI / unittest, JSON Schema, WebSocket, Opus 16 kHz mono 20 ms, OpenClaw 2026.7.1-1.

---

### Task 1: Promote the full Voice Gateway into the formal project

**Files:**
- Create: `tools/test_voice_gateway_layout.py`
- Create: `gateway/**` from the active restore worktree, excluding `.venv`, `.env`, `.pytest_cache`, `__pycache__`, recordings, logs, and local serial state
- Create: `contracts/**` from the active restore worktree
- Modify: `README.md`

**Steps:**
1. Write a layout test requiring `gateway/pyproject.toml`, the Voice Gateway package, its official lab test script, and shared schemas; forbid copied runtime/secrets directories.
2. Run the test and verify it fails because `gateway/` and `contracts/` are absent.
3. Copy only tracked source/config/test files from the active restore worktree, preserving its current working-tree fixes but excluding local runtime data.
4. Update the root README so `gateway/` is authoritative and `endpoint-gateway/` is explicitly legacy/reference-only.
5. Run the layout test and `gateway/tests/run_lab_tests.sh`; require both to pass.

### Task 2: Repair the recording-start trigger contract

**Files:**
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/include/sesame_voice/voice_turn_detector.h`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_turn_detector.cpp`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/include/sesame_voice/voice_controller.h`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_controller.cpp`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/test/test_voice_turn_detector.cpp`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_protocol/test/test_control_event.cpp`
- Create: `gateway/tests/test_firmware_contract_compatibility.py`

**Steps:**
1. Add failing tests for distinct manual, wakeword, and follow-up trigger events and for Gateway rejection of an empty `listen.start` payload.
2. Run the focused firmware and Gateway tests and observe the intended failures.
3. Carry a typed capture trigger into `begin_listening()` and serialize `payload.trigger` as `manual`, `wakeword`, or `followup`.
4. Replace the old serializer test that blessed `listen.start {}` with valid trigger fixtures.
5. Re-run focused tests and the full firmware host/Gateway suites.

### Task 3: Make ESP32 uplink delivery sequence-safe

**Files:**
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_transport/include/sesame_transport/gateway_client.h`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_transport/gateway_client.cpp`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_controller.cpp`
- Create or modify: focused host tests under `components/sesame_transport/test/` and `components/sesame_voice/test/`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/tests/run_host_tests.sh`

**Steps:**
1. Add a failing host test proving a failed send does not consume an audio sequence and transport failure is observable.
2. Port the bounded outbound queue/worker pattern from the known-good older firmware rather than blocking the capture task for up to two seconds.
3. Increment sequence only after a frame is accepted by the outbound queue; on overflow/failure, cancel the turn with an explicit transport reason instead of silently continuing.
4. Run focused tests, full host tests, and an ESP-IDF build.

### Task 4: Repair downlink turn binding and playback state transitions

**Files:**
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/include/sesame_voice/voice_controller.h`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_controller.cpp`
- Modify/create: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/test/*`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/tests/run_host_tests.sh`

**Steps:**
1. Add failing tests for a `test_` TTS turn preserving its exact turn ID in playback telemetry, pause/resume changing renderer state, and flush returning the turn state to Idle.
2. Bind every accepted `tts.start` to its top-level turn ID and use that binding for `playback.stats`.
3. Handle `tts.pause` and `tts.resume` for the active generation; make flush clear the queue, stop renderer output, restore Idle, and re-enable capture.
4. Re-run focused and full firmware tests.

### Task 5: Align Gateway and ESP32 jitter-buffer flow control

**Files:**
- Modify: `gateway/tests/test_streaming_downlink.py`
- Modify: `gateway/tests/test_local_pcm_test.py`
- Modify: `gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py`

**Steps:**
1. Change tests to require a 30-packet bootstrap for both normal speech and local PCM, and add the failing `max_seen=29/30` boundary case.
2. Verify the old 40/50 behavior fails the new tests.
3. Send the same 30-frame lead the ESP32 uses, confirm bootstrap at the device's reported start threshold, and retain bounded 20/40 refill watermarks afterward.
4. Add a regression test proving the sender does not enter repeated 250 ms waits after playback begins.
5. Run focused tests and the full Gateway lab suite.

### Task 6: Use the real OpenClaw run ID for cancellation

**Files:**
- Modify: `gateway/tests/test_openclaw_retry.py`
- Modify: `gateway/apps/voice_gateway/src/sesame_voice_gateway/openclaw/client.py`

**Steps:**
1. Replace the test that expects the idempotency key as `runId` with a failing fake-WebSocket test that receives `run_real_001`, cancels, and requires abort of that exact ID.
2. Move best-effort abort into the chat attempt after the `chat.send` acknowledgement exposes the real run ID; do not issue a guessed abort before an ID exists.
3. Verify timeout and cancellation retain bounded latency and safe errors.
4. Run focused and full Gateway tests.

### Task 7: Enforce one web-search authority

**Files:**
- Modify: `ops/openclaw/config/agents.template.json`
- Modify: `ops/openclaw/workspaces/shared/TOOLS.md`
- Modify: `ops/openclaw/workspaces/sesame*/AGENTS.md`
- Create: `tools/test_openclaw_search_boundary.py`

**Steps:**
1. Add a failing policy test proving Sesame agents cannot call OpenClaw-native `web_search` and that the Gateway tool loop remains enabled.
2. Remove native `web_search` permission from the four Sesame agent templates and state that agents request search only through the structured `requires_tool` response consumed by Gateway.
3. Keep the Gateway's one-search, bounded-query, bounded-source validation unchanged.
4. Run the new policy test, Gateway tool-call tests, and `openclaw config validate` without overwriting the user's live global configuration.

### Task 8: Add an actual playback-completion acknowledgement

**Files:**
- Modify: `contracts/schemas/control-event.v1.schema.json`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/playback_telemetry.*`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_controller.cpp`
- Modify: `gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py`
- Modify: firmware and Gateway playback telemetry tests

**Steps:**
1. Add failing tests requiring final telemetry with `playback_complete=true`, exact `rendered_frames`, zero underflow/decode/I2S errors, and matching turn/generation.
2. Emit final telemetry only after the ESP32 queue is drained and I2S write completion is observed.
3. Record a separate `tts.playback.completed` stage in Gateway; keep `tts.downlink` as transport-send completion rather than audible-playback proof.
4. Add a bounded wait/diagnostic for missing completion without deadlocking legacy devices.
5. Run schema, firmware, and Gateway suites.

### Task 9: Remove remaining playback correctness hazards

**Files:**
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_audio/audio_hal.cpp`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_controller.cpp`
- Modify/create: audio conversion and playback stack tests

**Steps:**
1. Add a failing conversion test for negative PCM samples and replace signed left-shift with defined multiplication/widening.
2. Move large playback/I2S working buffers out of the 8 KiB task stack or increase/measure the stack with an explicit high-water diagnostic.
3. Report decode failure, I2S short write, sequence gap, queue overflow, and underflow counters instead of silently flushing.
4. Re-run host tests and ESP-IDF build with warnings as errors where available.

### Task 10: Full software and hardware re-verification

**Files:**
- Modify: `.codex/handoffs/task-end-to-end-voice-audit.md`
- Create: a timestamped verification record under `flash-diagnostics/` only if hardware is connected

**Steps:**
1. Run project layout/policy/cross-contract tests.
2. Run the full formal Gateway suite and build its wheel.
3. Run all firmware host tests, flash-script tests, and a fresh ESP-IDF build.
4. Confirm Gateway/OpenClaw health and that the service command points to the formal `gateway/` source; do not expose credentials.
5. If ESP32 is present, flash the application using the repaired re-enumeration-safe flow, then run one stable-knowledge turn and one current-information/search turn.
6. Require continuous uplink/downlink sequences, completed ASR/OpenClaw/TTS/search stages, final playback acknowledgement, and audible MAX98357 output. If hardware is absent, report software completion separately and leave hardware acceptance explicitly pending.

## Outcome (2026-08-22)

Tasks 1-9 and the software portion of Task 10 are complete. Final evidence is
recorded in `.codex/handoffs/task-end-to-end-voice-audit.md`. The Gateway suite
passes 146/146, firmware host checks pass, and ESP-IDF produces a `0x1c87a0`
image with 41% app-partition headroom. Runtime health and console return 200
from the formal deployed source. Hardware flashing and acoustic acceptance are
pending because no ESP32 USB serial device is currently connected.
