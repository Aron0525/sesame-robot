# Restore Full Web Controls Implementation Plan

> **For Codex:** Execute this plan in small verified steps; preserve the current independent speaker/TTS implementation.

**Goal:** Restore the original browser control surface so its robot actions, expressions, servo sliders, motion settings, wake-word threshold, and emergency stop reach the ESP32 and operate the hardware.

**Architecture:** Keep the current WSS gateway and new audio path intact.  Port the original v1.6 servo, motion-runner, OLED, and browser-control modules into the formal firmware.  Extend the existing WSS protocol with the validated `operator.control` event that the legacy console emits.  The gateway validates and forwards a narrow control contract; the firmware executes it through the same single motion owner used by the local web controller.

**Tech Stack:** ESP-IDF 5.5, Arduino-ESP32 component, C++20 host tests, Python unittest gateway tests, ESP32-S3, FastAPI/Uvicorn gateway.

---

### Task 1: Lock down the browser-to-device control contract with tests

**Files:**
- Modify: `endpoint-gateway/src/sesame_endpoint_gateway/app.py`
- Create/modify: `endpoint-gateway/tests/test_legacy_control.py`

1. Add failing tests for each supported legacy control kind (`action`, `expression`, `servo`, `settings`, `wakeword_settings`, `stop`).
2. Require strict allow-lists and numeric ranges; reject malformed or unsupported browser input.
3. Make the local-control endpoint deliver an `operator.control` WSS event instead of reducing controls to the old four action names.
4. Run `./.venv/bin/python -m unittest discover -s tests` in `endpoint-gateway`.

### Task 2: Restore the firmware control catalog and safety routing with host tests

**Files:**
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_robot/action_policy.cpp`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_robot/test/test_action_policy.cpp`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_protocol/*`
- Modify: `firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/*`

1. Add failing host assertions for the original action catalogue and `operator.control` protocol type.
2. Port the original robot control catalogue and validated event handling, keeping voice/TTS data events unchanged.
3. Return an `action.result` acknowledgement for every accepted or rejected control request.
4. Run `bash tests/run_host_tests.sh`.

### Task 3: Port the independent physical control stack

**Files:**
- Copy/adapt: v1.6 `components/sesame_robot/{esp32_servo_driver,motion_plan,motion_executor,servo_calibration,control_catalog}*`
- Copy/adapt: v1.6 `components/sesame_ui/*`
- Copy/adapt: v1.6 `components/sesame_web/*`
- Modify: corresponding component `CMakeLists.txt` files and formal `main/app_main.cpp`

1. Reuse the original eight-servo pin map, safe motion executor, and single legacy motion runner.
2. Initialize those modules before voice control, but retain the current mic/speaker I2S configuration and playback task.
3. Route both local HTTP and gateway WSS commands through `RobotAdapter`, so two control sources cannot fight for servo ownership.
4. Build with ESP-IDF.

### Task 4: Validate on the real device and deploy

**Files:**
- Modify: `.codex/handoffs/task-new-speaker-playback.md`

1. Flash only the application partition, preserving the already-provisioned NVS network/gateway configuration.
2. Confirm the ESP32 reconnects to the gateway after flashing.
3. Verify a non-moving stop request end-to-end, then perform one controlled motion only with the robot placed safely.
4. Record the exact verification and any control not present in the original firmware.
