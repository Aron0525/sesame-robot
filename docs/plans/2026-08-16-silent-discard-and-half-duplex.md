# Silent Discard and Half-Duplex Audio Implementation Plan

> **For Codex:** Use `/Users/mac/.codex/skills/executing-plans/SKILL.md` to implement this plan task-by-task.

**Goal:** Silently end blank or filler-only voice turns, and keep TTS playback strictly half-duplex while preserving the existing three-second follow-up window after completed TTS.

**Architecture:** The gateway classifies the completed ASR transcript before OpenClaw/TTS and returns a typed silent-discard outcome.  It sends a new `turn.complete` control event so the ESP32 can leave `thinking` without fabricating a TTS response.  The firmware disables microphone/VAD/MultiNet processing during TTS; only BOOT remains an interrupt path.

**Tech Stack:** Python 3.12/FastAPI gateway, JSON Schema v1 control protocol, ESP-IDF C++ firmware, pytest and host C++ tests.

---

### Task 1: Define text validity behavior

**Files:**
- Create: `gateway/apps/voice_gateway/src/sesame_voice_gateway/text_validity.py`
- Create/Modify test: `gateway/tests/test_text_validity.py`

1. Add failing tests for blank text, filler-only repetitions, mixed filler plus meaningful text, and short semantic commands.
2. Run the focused pytest file and observe the import/function failure.
3. Implement normalization and a three-way classifier: `blank`, `filler_only`, `valid`.
4. Re-run the focused tests.

### Task 2: Make the pipeline return a silent-discard outcome

**Files:**
- Modify: `gateway/apps/voice_gateway/src/sesame_voice_gateway/pipeline.py`
- Modify test: `gateway/tests/test_privacy_pipeline.py`

1. Add failing pipeline tests proving no-speech and filler-only input never call agent/TTS and return a discard outcome.
2. Run focused pytest and observe the missing outcome/type failure.
3. Add `SilentDiscard`, preserve test-recording capture, mark observability as discarded/skipped, and return before OpenClaw/TTS.
4. Re-run focused tests.

### Task 3: Deliver the completion event to the ESP32

**Files:**
- Modify: `contracts/schemas/control-event.v1.schema.json`
- Modify: `gateway/apps/voice_gateway/src/sesame_voice_gateway/protocol/control.py`
- Modify: `gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py`
- Modify test: `gateway/tests/test_gateway_protocol_hardening.py`
- Modify: `firmware/esp32_voice_idf/components/sesame_protocol/include/sesame_protocol/control_event.h`
- Modify: `firmware/esp32_voice_idf/components/sesame_protocol/control_event.cpp`
- Modify: `firmware/esp32_voice_idf/components/sesame_protocol/include/sesame_protocol/turn_state.h`
- Modify: `firmware/esp32_voice_idf/components/sesame_protocol/turn_state.cpp`
- Modify: `firmware/esp32_voice_idf/components/sesame_voice/voice_controller.cpp`
- Modify test: `firmware/esp32_voice_idf/tests/test_turn_state.cpp`

1. Add failing gateway/schema and turn-state tests for a valid `turn.complete` discard.
2. Run the focused tests and observe the expected failure.
3. Add the strict payload (`outcome=discard`, approved reason list), gateway sender, and ESP32 active-turn validation.  The ESP32 must transition `thinking -> idle`, clear the active turn, and must not start follow-up listening.
4. Re-run the focused tests.

### Task 4: Enforce strict half-duplex TTS

**Files:**
- Modify: `firmware/esp32_voice_idf/components/sesame_voice/voice_turn_detector.cpp`
- Modify: `firmware/esp32_voice_idf/components/sesame_voice/include/sesame_voice/voice_turn_detector.h`
- Modify: `firmware/esp32_voice_idf/components/sesame_voice/voice_controller.cpp`
- Modify: `firmware/esp32_voice_idf/components/sesame_voice/include/sesame_voice/wake_capture_policy.h`
- Modify tests: `firmware/esp32_voice_idf/tests/test_voice_turn_detector.cpp`, `firmware/esp32_voice_idf/tests/test_wake_capture_policy.cpp`, `firmware/esp32_voice_idf/tests/verify_downlink_transport.py`

1. Change the old barge-in tests to require that wake/VAD input is ignored in `kTtsPlaying` and that capture policy rejects TTS-active microphone processing.
2. Run host tests and observe expected failure.
3. Remove playback wake arming and microphone reads/feeds while TTS is active; retain BOOT button processing and the existing post-TTS follow-up wait.
4. Re-run host tests.

### Task 5: Verify end-to-end build

1. Run complete gateway pytest suite.
2. Run firmware host tests.
3. Build ESP-IDF firmware with the project’s configured ESP-IDF export command.
4. Inspect the diff against the stated requirements.  Do not flash hardware without a fresh explicit confirmation.
