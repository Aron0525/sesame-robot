# End Dialogue Session Implementation Plan

> **For Claude:** Use `${SUPERPOWERS_SKILLS_ROOT}/skills/collaboration/executing-plans/SKILL.md` to implement this plan task-by-task.

**Goal:** Allow a user to end only the current dialogue by voice, acknowledge once, return the robot to wake-word standby, and give the next wake-up a fresh OpenClaw conversation.

**Architecture:** Add a deterministic, Gateway-local end-intent detector between ASR and OpenClaw. It produces a dialogue transition plus a fixed reply; therefore direct or confirmation replies never call OpenClaw. Carry a `follow_up` flag in `tts.stop`; firmware uses it to choose between the current three-second follow-up listener and wake-word standby.

**Tech Stack:** Python 3.12, FastAPI, `unittest`, JSON Schema, WebSocket control protocol, C++17, ESP-IDF, cJSON, ESP32-S3 firmware.

---

## Approved product rules

| Input state | Exact normalized utterance | Result |
|---|---|---|
| `active` | `结束对话`、`结束聊天`、`退出对话`、`退出聊天`、`关闭对话`、`关闭聊天`，以及前置“请/帮我/现在”和末尾“吧/了” | End directly |
| `active` | `拜拜`、`再见`、`先这样`、`我先走了`、`不聊了` | Ask once: `你要结束本次对话吗？` |
| `confirming_end` | `是`、`好`、`确认`、`结束`、`退出` | End directly |
| `confirming_end` | `不是`、`继续`、`继续聊`、`取消` | Return to active; answer with a fixed acknowledgement or process the rest of the normal turn |
| any | `什么时候结束`、`结束后做什么`、`“退出”是什么意思` | Normal dialogue; no lifecycle transition |

- Confirmation expires after 8 seconds. Expiry is checked when the next valid ASR transcript arrives; no background timer is needed.
- End acknowledgement is fixed: `好的，本次对话结束。`
- Ending a dialogue retains no active OpenClaw sandbox for that conversation. It does **not** disconnect the device WebSocket or Wi-Fi.
- Existing action behavior remains unchanged: OpenClaw-originated motion remains disabled.

## Target runtime flow

```text
ASR final text
  -> EndIntentDetector(text, current dialogue state, monotonic time)
      -> continue: OpenClaw -> normal TTS -> tts.stop { follow_up: true }
      -> confirm: fixed confirmation TTS -> tts.stop { follow_up: true }
      -> end: fixed ending TTS -> tts.stop { follow_up: false }

ESP32 receives terminal tts.stop
  -> drains the ending TTS
  -> resets turn detector
  -> returns to wake-word standby
  -> does not send a follow-up listen.start

Next wake/listen.start
  -> Gateway creates a new conversation_id
  -> the old OpenClaw sandbox remains removed
```

## Scope and compatibility decisions

- Gateway source of truth: `/Users/mac/Desktop/2/gateway`.
- Firmware source used by the current flashing workflow: `/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF`.
- Add `follow_up` to `tts.stop.payload` as an **optional** boolean in the v1 schema. Gateway sends it explicitly. Updated firmware defaults a missing field to `true`, so an older Gateway retains current behavior.
- Do not add a new WebSocket event type. Extending `tts.stop` is the smallest protocol change.
- `ConversationRegistry.close()` returns the closed `Conversation` after ownership validation. The caller uses that ID to remove the matching OpenClaw sandbox.
- The ending turn is the active task that sends the final TTS. Do not call `_cancel_active_turn()` on that task itself; its epoch checks already prevent older turns from writing after cancellation.

## Task 1: Add deterministic end-intent policy with tests

**Files:**
- Create: `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/end_intent.py`
- Create: `/Users/mac/Desktop/2/gateway/tests/test_end_intent.py`

**Step 1: Write failing decision-table tests**

```python
from sesame_voice_gateway.end_intent import DialogueState, EndIntent, EndIntentDetector


def test_direct_end_has_no_confirmation() -> None:
    decision = EndIntentDetector().decide("请结束本次对话吧", DialogueState.ACTIVE, now=10.0)
    assert decision.intent is EndIntent.END
    assert decision.reply == "好的，本次对话结束。"
    assert decision.follow_up is False


def test_question_containing_end_is_normal_dialogue() -> None:
    decision = EndIntentDetector().decide("这段对话什么时候结束？", DialogueState.ACTIVE, now=10.0)
    assert decision.intent is EndIntent.CONTINUE


def test_bye_requires_confirmation_then_yes_ends() -> None:
    detector = EndIntentDetector()
    first = detector.decide("拜拜", DialogueState.ACTIVE, now=10.0)
    second = detector.decide("是", DialogueState.CONFIRMING_END, now=12.0)
    assert first.intent is EndIntent.CONFIRM
    assert second.intent is EndIntent.END
```

**Step 2: Run the test and confirm it fails**

```bash
cd "/Users/mac/Desktop/2/gateway"
PYTHONPATH=apps/voice_gateway/src ./.venv/bin/python -m unittest tests.test_end_intent -v
```

Expected: import failure because `end_intent.py` does not exist.

**Step 3: Implement the smallest policy**

Create these immutable types:

```python
class DialogueState(StrEnum):
    ACTIVE = "active"
    CONFIRMING_END = "confirming_end"
    ENDED = "ended"

class EndIntent(StrEnum):
    CONTINUE = "continue"
    CONFIRM = "confirm"
    CANCEL_CONFIRMATION = "cancel_confirmation"
    END = "end"

@dataclass(frozen=True, slots=True)
class EndIntentDecision:
    intent: EndIntent
    reply: str | None = None
    follow_up: bool = True
```

Normalize with Unicode NFKC, trim whitespace, lowercase ASCII, and strip terminal punctuation. Match only full utterances or the explicit direct-command pattern. The detector must remain fully local and must not call an LLM.

**Step 4: Add expiry and all phrase tests**

Include tests for all table entries, an 8-second expiry, and text that merely mentions an end word. Use a passed `now` value; do not mock global time.

**Step 5: Run the test suite**

```bash
cd "/Users/mac/Desktop/2/gateway"
PYTHONPATH=apps/voice_gateway/src ./.venv/bin/python -m unittest tests.test_end_intent -v
```

Expected: all end-intent tests pass.

## Task 2: Add explicit conversation close support

**Files:**
- Modify: `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/conversations.py`
- Create: `/Users/mac/Desktop/2/gateway/tests/test_conversations.py`

**Step 1: Write failing registry tests**

```python
def test_close_removes_only_the_owned_conversation() -> None:
    registry = ConversationRegistry(ttl_seconds=300, clock=lambda: 10.0)
    conversation = registry.resolve(requested_conversation_id=None, device_id="dev_1", user_id="user_1")

    closed = registry.close(
        conversation_id=conversation.conversation_id,
        device_id="dev_1",
        user_id="user_1",
    )

    assert closed == conversation
    with pytest.raises(ConversationExpired):
        registry.resolve(
            requested_conversation_id=conversation.conversation_id,
            device_id="dev_1",
            user_id="user_1",
        )
```

Use `unittest` assertions rather than adding pytest if the project has no pytest dependency.

**Step 2: Run the test and confirm it fails**

```bash
cd "/Users/mac/Desktop/2/gateway"
PYTHONPATH=apps/voice_gateway/src ./.venv/bin/python -m unittest tests.test_conversations -v
```

**Step 3: Implement `ConversationRegistry.close()`**

- Resolve the exact ID.
- Keep the existing ownership check.
- Delete it from `_conversations`.
- Return the deleted `Conversation`.
- Raise `ConversationExpired` for an unknown or expired ID.

**Step 4: Run the focused registry tests**

Run the command in Step 2. Expected: PASS.

## Task 3: Route transcript decisions before OpenClaw

**Files:**
- Modify: `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/pipeline.py`
- Modify: `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/providers/base.py`
- Modify: `/Users/mac/Desktop/2/gateway/tests/test_privacy_pipeline.py`

**Step 1: Write a failing pipeline test**

Use fake ASR, fake Agent, and fake TTS. Submit `结束对话` and assert:

```python
assert fake_agent.calls == []
assert result.agent.text == "好的，本次对话结束。"
assert result.follow_up_after_tts is False
assert result.end_intent is EndIntent.END
```

Add a second test where `拜拜` returns the fixed confirmation reply, keeps `follow_up_after_tts=True`, and leaves `fake_agent.calls == []`.

**Step 2: Run the focused test and confirm it fails**

```bash
cd "/Users/mac/Desktop/2/gateway"
PYTHONPATH=apps/voice_gateway/src ./.venv/bin/python -m unittest \
  tests.test_privacy_pipeline.PrivacyAndPipelineTest -v
```

**Step 3: Add explicit result metadata**

Extend `TurnResult` with defaults so unrelated tests stay compatible:

```python
follow_up_after_tts: bool = True
end_intent: EndIntent = EndIntent.CONTINUE
```

Pass the session’s `DialogueState` into `ConversationContext`. Inject `EndIntentDetector` into `ConversationPipeline`; default to one deterministic detector.

After a non-empty validated ASR result and before `_agent.reply()`, decide the intent:

- `CONTINUE`: retain the current OpenClaw path.
- `CONFIRM`, `CANCEL_CONFIRMATION`, `END`: create the fixed `AgentResult`, skip OpenClaw, and use the decision’s follow-up value.
- `NoSpeechDetected`: retain the current no-speech reply and preserve the prior dialogue state.

**Step 4: Run focused pipeline tests**

Run the command in Step 2. Expected: PASS.

## Task 4: Add dialogue state and close the sandbox in the Gateway

**Files:**
- Modify: `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py`
- Modify: `/Users/mac/Desktop/2/gateway/tests/test_gateway_protocol_hardening.py`
- Modify: `/Users/mac/Desktop/2/gateway/tests/test_interruptible_turns.py`

**Step 1: Write failing Gateway lifecycle tests**

Cover these behaviors:

1. A direct ending turn closes the registry entry, schedules one sandbox reaper call with the old OpenClaw session key, and sends only the fixed ending reply.
2. A terminal turn sends `tts.stop` with `follow_up: false`.
3. A confirmation turn updates `DeviceSession.dialogue_state` to `CONFIRMING_END` and sends `follow_up: true`.
4. The next `listen.start` after `ENDED` creates and assigns a new `conversation_id` before building `ConversationContext`.
5. A stale older task has no opportunity to send controls after the terminal turn’s epoch is current.

Use a fake `SandboxReaper`; never execute the real `openclaw sessions delete` command in tests.

**Step 2: Run the focused tests and confirm they fail**

```bash
cd "/Users/mac/Desktop/2/gateway"
PYTHONPATH=apps/voice_gateway/src ./.venv/bin/python -m unittest \
  tests.test_gateway_protocol_hardening \
  tests.test_interruptible_turns -v
```

**Step 3: Implement state transitions**

Add to `DeviceSession`:

```python
dialogue_state: DialogueState = DialogueState.ACTIVE
end_confirmation_expires_at: float | None = None
```

In `_run_turn`, after obtaining `TurnResult` and before sending the result:

- `CONFIRM`: set `CONFIRMING_END`, with `time.monotonic() + 8`.
- `CANCEL_CONFIRMATION` or normal text after an expired confirmation: set `ACTIVE`, clear expiry.
- `END`: set `ENDED`; retain the current terminal task so it can send its ending TTS; call `ConversationRegistry.close()` after the terminal result is queued; remove the exact OpenClaw sandbox through the injected reaper. Log reaper failure and keep the dialogue state ended.

At the next `listen.start`, if state is `ENDED`, resolve a new conversation with `requested_conversation_id=None`, assign it to `session.conversation_id`, then restore `ACTIVE` before creating `ConversationContext`.

Keep the device WebSocket open throughout.

**Step 4: Run the focused lifecycle tests**

Run the command in Step 2. Expected: PASS.

## Task 5: Extend the v1 `tts.stop` contract

**Files:**
- Modify: `/Users/mac/Desktop/2/contracts/schemas/control-event.v1.schema.json`
- Modify: `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/protocol/control.py` only if its literal/model requires it
- Modify: `/Users/mac/Desktop/2/gateway/apps/voice_gateway/src/sesame_voice_gateway/app.py`
- Modify: `/Users/mac/Desktop/2/gateway/tests/test_response_plan.py`

**Step 1: Write a failing protocol test**

Parse a `tts.stop` event with:

```json
{"generation_id":7,"reason":"completed","follow_up":false}
```

Assert it is accepted. Preserve the existing `tts.stop` test without `follow_up` and assert it remains accepted.

**Step 2: Run the test and confirm it fails**

```bash
cd "/Users/mac/Desktop/2/gateway"
PYTHONPATH=apps/voice_gateway/src ./.venv/bin/python -m unittest tests.test_response_plan -v
```

**Step 3: Implement the compatible field**

Add optional `follow_up: {"type": "boolean"}` to `ttsStopPayload`; do not add it to `required`.

Change `_send_turn_result()` to emit:

```python
payload={
    "generation_id": result.generation_id,
    "reason": "completed",
    "follow_up": result.follow_up_after_tts,
}
```

Every normal, no-speech, and confirmation result emits `true`; only the final ending reply emits `false`.

**Step 4: Run focused protocol tests**

Run the command in Step 2. Expected: PASS.

## Task 6: Honor terminal TTS in ESP32 firmware

**Files:**
- Modify: `/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/include/sesame_voice/voice_controller.h`
- Modify: `/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_voice/voice_controller.cpp`
- Modify: `/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF/components/sesame_protocol/test/test_control_event.cpp` if the firmware protocol validation needs a new payload assertion
- Modify: `/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF/tests/run_host_tests.sh` only if a new host test is required

**Step 1: Add a failing device-facing test or deterministic parser test**

The test must prove both cases:

```text
missing follow_up -> follow-up listener remains enabled
follow_up=false -> no start_follow_up call after the matching TTS drains
```

Prefer an existing C++ control-event test if it can observe payload parsing. If `VoiceController` cannot be host-instantiated without hardware, add the smallest production-owned playback-completion helper and test that helper; do not use source-text matching as a behavioral test.

**Step 2: Run host tests and record the pre-change failure**

```bash
cd "/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF"
bash tests/run_host_tests.sh
```

**Step 3: Implement the terminal flag**

- Add `bool follow_up_after_tts_{true};` to `VoiceController`.
- Change `finish_tts()` to accept the parsed boolean.
- In `process_control_json()`, read `payload.follow_up`; when absent or not a JSON boolean, use `true`.
- In `complete_tts_if_drained()`, retain the current reset and amplifier shutdown. Call `start_follow_up()` only when `follow_up_after_tts_` is true. For false, remain in normal wake-word capture state and send no `listen.start` event.
- Reset the flag to `true` in `begin_tts()` and `flush_tts()` to prevent one terminal result affecting later replies.

**Step 4: Run firmware host tests**

Run the command in Step 2. Expected: PASS.

## Task 7: Full verification, build, and hardware acceptance

**Files:**
- Modify only if a failing verification identifies a narrow defect.
- Create: `/Users/mac/Documents/sesame robot/output/<timestamp>-end-dialogue-session/` verification artifact at implementation time.

**Step 1: Run full Gateway tests**

```bash
cd "/Users/mac/Desktop/2/gateway"
PYTHONPATH=apps/voice_gateway/src ./.venv/bin/python -m unittest discover -s tests -v
```

Expected: all tests pass.

**Step 2: Run firmware host tests and build**

```bash
cd "/Users/mac/Documents/sesame robot/firmware-work/Sesame_Robot_V3_IDF"
bash tests/run_host_tests.sh
cmake --build build --target app -- -j2
```

Expected: host tests pass and the ESP-IDF app image builds.

**Step 3: Device acceptance checklist after flashing**

1. Say `结束对话` after a normal reply: hear the ending acknowledgement, then observe wake-word standby rather than a three-second follow-up window.
2. Say `拜拜`: hear the confirmation; say `是`; verify ending behavior.
3. Say `什么时候结束？`: verify a normal OpenClaw reply and normal follow-up listening.
4. After ending, wake the robot and ask a new question; verify the Gateway uses a new `conversation_id` and OpenClaw session.
5. Inspect the Gateway observability timeline: terminal turn has no OpenClaw stage and `tts.stop.follow_up=false`.

**Step 4: Package verification and rollback**

Before editing, hash and preserve only the files in this plan. Operate on copies, produce a unified patch, execute both rollback and re-apply scripts, then record baseline/final hashes, test commands, literal outputs, and exit statuses under the output artifact directory.

## Non-goals

- No speech-to-text semantic model on ESP32.
- No change to ASR, Web Search, knowledge bases, agent modes, robot actions, or microphone hardware.
- No device disconnect after a dialogue ends.
- No source cleanup or commits covering pre-existing unrelated changes in `/Users/mac/Desktop/2`.
