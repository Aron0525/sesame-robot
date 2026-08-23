# Sesame V3 Architecture Learning

- **Task:** Help the user understand and be able to reproduce the Desktop/2 Sesame V3 architecture.
- **Objective:** Explain deployment units, runtime flow, contracts, configuration, validation, security boundaries and code ownership in progressive Chinese lessons.
- **Session:** Current Codex teaching workspace.
- **Project:** Source evidence at /Users/mac/Desktop/2; learning artifacts at /Users/mac/Documents/sesame robot.

## Current State

- Existing lessons 0007–0012 include earlier Arduino/Demo-era claims; current v1.5 code and root README/contracts take precedence where they conflict.
- Lessons 0013 and 0014 establish the current project map and reproducible engineering architecture.
- Lesson 0015 and the code atlas establish the first source-reading path: `app_main` → `VoiceController` → Gateway `DeviceSession` → `ConversationPipeline` → TTS downlink.
- Lesson 0016 and the wiring map distinguish the target GPIO/power wiring from current physical evidence. No photos or runtime evidence prove every current wire.
- The project working tree contains user changes and untracked documentation/hardware work. Preserve it; this teaching task does not edit Desktop/2 source.

## Evidence

- Firmware entry: firmware/esp32_voice_idf/main/app_main.cpp.
- Gateway composition: gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py.
- Pipeline: gateway/apps/voice_gateway/src/sesame_voice_gateway/pipeline.py.
- Settings/security: gateway/.../config.py, docs/security.md and contracts/.
- Setup: docs/setup/new-machine-setup-guide.md.
- Current known conflict: current Gateway strips Agent actions in client.py and app.py, while some protocol/docs examples still show actions.

## Completed

- Created lessons/0014-sesame-reproducible-architecture.html.
- Created reference/sesame-v3-rebuild-blueprint.html.
- Created lessons/0015-sesame-code-reading-first-tour.html.
- Created reference/sesame-v3-code-atlas.html.
- Created lessons/0016-sesame-current-wiring.html and reference/sesame-v3-wiring-map.html.
- Verified artifact presence and key content; no source code or runtime was changed.

## Next Step

If physical photos become available, reconcile each real wire against the target map before further electrical testing. Otherwise continue with `VoiceController::process_control_json()` and `GatewayClient` discovery/TLS/WSS construction.
