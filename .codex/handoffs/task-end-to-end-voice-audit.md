# Sesame V3 End-to-End Voice Audit

- **Task:** Audit the current ESP32 -> Gateway -> ASR -> OpenClaw -> optional
  web search -> TTS -> WSS -> MAX98357 voice chain with multiple independent
  reviewers.
- **Objective:** Determine whether the current project is internally compatible,
  reproducible, and ready for hardware acceptance without changing code.
- **Session:** 2026-08-22.
- **Project:** `/Users/mac/Documents/sesame robot`.
- **Current State:** Software repair and independent re-audit complete. Formal
  Gateway runtime is healthy; ESP32 is not connected, so flash and acoustic
  acceptance remain pending.

## Conclusion

- Overall software result: **passed; hardware acceptance pending**.
- The formal firmware and Gateway now agree on recording triggers, turn IDs,
  sequence ownership, playback telemetry and pause/resume/flush behavior.
- The full Voice Gateway and shared contracts now live in this project. Port
  8766 runs from `~/.local/share/sesame-robot-runtime`, with a private runtime
  `.env`/TLS directory and no dependency on the old Desktop/2 source checkout.
- Independent uplink and downlink re-audits report no remaining P0/P1 software
  defect in the reviewed voice path. Static/concurrency tests cannot replace
  real dual-core, Wi-Fi and electrical acceptance.

## Repairs Landed

- Typed `listen.start.trigger` for manual/wakeword/follow-up and empty
  `listen.stop` payload compatibility.
- Bounded non-blocking uplink sender queue; sequences commit only after queue
  acceptance; reconnect is fail-closed on gaps or send failures.
- Reconnect handshake is mutex-serialized so every new epoch starts with
  `session.hello sequence=0`; BOOT state does not advance before session ready.
- Manual BOOT recording ends on the second press or the 30-second safety limit,
  not the generic 800 ms VAD/10-second detector endpoint.
- Exact TTS turn binding, pause/resume/flush state, 30-frame bootstrap and
  max=29/playback-started boundary handling.
- Full playback telemetry with rendered/error counters, 160 ms TX DMA tail,
  strict Gateway success validation and terminal failure acknowledgement.
- Playback claims its codec/I2S critical interval before generation validation;
  every listen turn resets the RX DMA queue before microphone capture.
- Defined signed PCM expansion, larger PSRAM task stacks and static I2S raw
  buffers.
- OpenClaw cancellation uses its actual acknowledged run ID. Only Gateway owns
  bounded web search; live Sesame agents are read-only and deny native search.

## Fresh Evidence

- Same-turn final Gateway suite: 146/146 passed.
- Firmware host suite exited 0; fresh ESP-IDF 5.5.4 build generated
  `sesame_robot_v3.bin` size `0x1c87a0`, with 41% of the app partition free.
- Layout and OpenClaw search-boundary tests: 2/2 each; live OpenClaw config
  validates.
- Live Gateway health and console return HTTP 200. Providers are DashScope ASR,
  OpenClaw, DashScope TTS, qwen-plus search and libopus.
- Runtime process and cwd are both under
  `~/.local/share/sesame-robot-runtime/gateway`; private `.env` and TLS key are
  mode 600.
- Current observability remains `devices=0`, `turns=0`, `events=0`; no USB
  ESP32 serial device is present.

## Next Step

Connect the ESP32-S3, flash the freshly built image, then run: (1) manual BOOT
recording with second-press stop, (2) one stable-knowledge turn, (3) one current
information/search turn, and (4) playback interruption/reconnect stress. Require
continuous sequence numbers, `session.hello sequence=0` after reconnect, zero
playback error counters, exact rendered-frame count and clear MAX98357 output.
