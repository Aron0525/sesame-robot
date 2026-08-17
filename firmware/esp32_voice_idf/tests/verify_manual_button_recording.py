#!/usr/bin/env python3
"""Verify the modular wake-word capture path and BOOT fallback."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTROLLER = ROOT / "components/sesame_voice/voice_controller.cpp"


def main() -> None:
    source = CONTROLLER.read_text(encoding="utf-8")
    assert "kRecordingPaused" not in source

    run_start = source.index("void VoiceController::run()")
    run_end = source.index("void VoiceController::maintain_gateway_connection", run_start)
    run_loop = source[run_start:run_end]
    button_poll = run_loop.index("gpio_get_level(kVoiceButton)")
    session_gate = run_loop.index("else if (session_ready_ && !tts_active_)")
    microphone_read = run_loop.index("audio_->read_microphone_frame")
    wake_feed = run_loop.index("wake_vad_.feed_pcm")
    wake_process = run_loop.index("process_wake_vad_signals")
    capture = run_loop.index("drain_pcm_uplink")
    # A manual recording must reach the uplink before wake/VAD work. The
    # detector is deliberately asynchronous, because an inference cycle must
    # never stretch the 20-ms ASR audio cadence.
    assert button_poll < session_gate < microphone_read < capture < wake_feed < wake_process
    assert "capture_session_.active()" in run_loop
    assert "button_.recording()" not in run_loop
    assert "wake_listening_" not in run_loop
    assert "if (wake_ack_active_)" in run_loop
    assert "session_ready_ && !tts_active_" in run_loop

    # The Gateway must be able to retain only user-marked training samples.
    # Therefore ESP32 labels the source of every listen.start event.
    assert 'kManualListenStartPayload[] = R"({"trigger":"manual"})"' in source
    assert 'kWakewordListenStartPayload[] = R"({"trigger":"wakeword"})"' in source
    button_handler_start = source.index("void VoiceController::handle_button")
    button_handler_end = source.index("void VoiceController::begin_wake_ack", button_handler_start)
    button_handler = source[button_handler_start:button_handler_end]
    assert "event != ButtonEvent::kPressed" in button_handler
    assert "capture_session_.active()" in button_handler
    assert "finish_listening(\"device_button\")" in button_handler
    assert "start_listening(timestamp, CaptureSource::kManual);" in button_handler
    start_listening = source[
        source.index("bool VoiceController::start_listening") : source.index(
            "void VoiceController::finish_listening"
        )
    ]
    assert "source == CaptureSource::kManual" in start_listening
    assert "capture_session_.start(source, capture_started_ms)" in start_listening
    assert "kManualListenStartPayload" in start_listening
    assert "kWakewordListenStartPayload" in start_listening

    engine = (
        ROOT / "components/sesame_voice/wake_vad_engine.cpp"
    ).read_text(encoding="utf-8")
    feed_start = engine.index("esp_err_t WakeVadEngine::feed_pcm")
    feed_end = engine.index("bool WakeVadEngine::read_signal", feed_start)
    feed = engine[feed_start:feed_end]
    assert "xQueueSend(audio_queue_" in feed
    assert "process_pcm" not in feed
    assert "void WakeVadEngine::processing_loop" in engine
    assert '"esp_afe_sr_models.h"' in engine
    assert "afe_config_init(\"M\", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF)" in engine
    assert "result->vad_state == VAD_SPEECH" in engine

    wake_handler_start = source.index("void VoiceController::process_wake_vad_signals")
    wake_handler_end = source.index("bool VoiceController::start_listening", wake_handler_start)
    wake_handler = source[wake_handler_start:wake_handler_end]
    assert "begin_wake_ack();" in wake_handler

    print("Realtime uplink and audible wake acknowledgement verified")


if __name__ == "__main__":
    main()
