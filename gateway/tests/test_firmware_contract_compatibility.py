from __future__ import annotations

import json
import unittest
from pathlib import Path

from sesame_voice_gateway.protocol.control import ControlProtocolError, parse_control_event


PROJECT_ROOT = Path(__file__).resolve().parents[2]
VOICE_CONTROLLER = (
    PROJECT_ROOT
    / "firmware-work"
    / "Sesame_Robot_V3_IDF"
    / "components"
    / "sesame_voice"
    / "voice_controller.cpp"
)
AUDIO_HAL = (
    PROJECT_ROOT
    / "firmware-work"
    / "Sesame_Robot_V3_IDF"
    / "components"
    / "sesame_audio"
    / "audio_hal.cpp"
)


def _listen_start(payload: dict[str, object]) -> str:
    return json.dumps(
        {
            "v": 1,
            "type": "listen.start",
            "session_id": "ses_contract",
            "turn_id": "turn_contract",
            "request_id": None,
            "sequence": 1,
            "timestamp_ms": 1,
            "payload": payload,
        }
    )


class FirmwareControlContractCompatibilityTest(unittest.TestCase):
    def test_gateway_requires_a_typed_capture_trigger(self) -> None:
        with self.assertRaisesRegex(ControlProtocolError, "trigger.*required"):
            parse_control_event(_listen_start({}))

        for trigger in ("manual", "wakeword", "followup"):
            event = parse_control_event(_listen_start({"trigger": trigger}))
            self.assertEqual(event.payload, {"trigger": trigger})

    def test_formal_firmware_no_longer_sends_empty_listen_start_payload(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        self.assertNotIn("ControlEventType::kListenStart, \"{}\"", source)
        self.assertIn("capture_trigger", source)
        self.assertIn(r'{\"trigger\":\"%s\"}', source)

    def test_listen_stop_keeps_its_empty_payload_contract(self) -> None:
        event = parse_control_event(
            json.dumps(
                {
                    "v": 1,
                    "type": "listen.stop",
                    "session_id": "ses_contract",
                    "turn_id": "turn_contract",
                    "request_id": None,
                    "sequence": 2,
                    "timestamp_ms": 2,
                    "payload": {},
                }
            )
        )
        self.assertEqual(event.payload, {})
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        self.assertNotIn(r'{\"reason\":\"%s\"}', source)

    def test_playback_stats_use_the_tts_turn_binding(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        self.assertIn("playback_turn_.bind(incoming_turn_id, generation_id)", source)
        self.assertIn("playback_turn_.turn_id()", source)
        for field in (
            "buffered_ms",
            "startup_packets",
            "min_buffered_packets",
            "dropped_packet_count",
            "stale_generation_count",
            "out_of_order_count",
            "sequence_discontinuity_count",
            "decode_last_us",
            "i2s_last_us",
            "rendered_frames",
            "playback_complete",
        ):
            self.assertIn(field, source)

    def test_firmware_handles_pause_resume_and_flushes_to_idle(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        self.assertIn('std::strcmp(type, "tts.pause")', source)
        self.assertIn('std::strcmp(type, "tts.resume")', source)
        self.assertIn(
            "turn_state_.apply(sesame::protocol::TurnEvent::kInterrupted)", source
        )

    def test_uplink_uses_a_bounded_sender_queue_without_sequence_gaps(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        self.assertIn("outbound_queue_ = xQueueCreate", source)
        self.assertIn("outbound_task_entry", source)
        self.assertNotIn("gateway_.send_binary(message.data(), message_size)", source)
        self.assertIn("if (enqueue_outbound_binary", source)
        self.assertIn("audio_sequence_.commit_if(true)", source)
        self.assertLess(
            source.index("if (enqueue_outbound_binary"),
            source.index(
                "audio_sequence_.commit_if(true)",
                source.index("if (enqueue_outbound_binary"),
            ),
        )
        self.assertIn("transport_fault_requested_", source)
        self.assertIn("gateway_.force_reconnect()", source)

    def test_reconnect_makes_hello_the_first_frame_of_the_new_epoch(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        send_control = source[
            source.index("esp_err_t VoiceController::send_control(") :
            source.index("esp_err_t VoiceController::enqueue_outbound_text")
        ]
        connected = source[
            source.index("void VoiceController::on_gateway_connected()") :
            source.index("void VoiceController::on_gateway_disconnected()")
        ]
        self.assertLess(
            send_control.index("xSemaphoreTake(control_send_mutex_"),
            send_control.index("!is_session_hello && !session_ready_"),
        )
        self.assertIn("send_session_hello_locked()", connected)
        self.assertLess(
            connected.index("xSemaphoreTake(control_send_mutex_"),
            connected.index("outbound_connection_epoch_.fetch_add(1)"),
        )
        self.assertLess(
            connected.index("control_sequence_.reset()"),
            connected.index("send_session_hello_locked()"),
        )
        self.assertLess(
            connected.index("send_session_hello_locked()"),
            connected.index("xSemaphoreGive(control_send_mutex_)"),
        )

    def test_button_state_does_not_advance_before_session_ready(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        run_loop = source[
            source.index("void VoiceController::run()") :
            source.index("void VoiceController::run_playback()")
        ]
        self.assertIn("if (session_ready_)", run_loop)
        self.assertIn("button_.reset();", run_loop)
        self.assertLess(
            run_loop.index("if (session_ready_)"),
            run_loop.index("button_.update(pressed, timestamp)"),
        )

    def test_playback_claims_busy_before_validating_a_dequeued_packet(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        playback = source[
            source.index("void VoiceController::run_playback()") :
            source.index("void VoiceController::handle_button")
        ]
        self.assertIn(
            "playback_busy_ = true;\n    if (packet.generation_id", playback
        )

    def test_terminal_playback_stats_are_sent_even_when_rendering_failed(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        playback = source[
            source.index("void VoiceController::run_playback()") :
            source.index("void VoiceController::handle_button")
        ]
        self.assertIn("queue_playback_stats(false, true);", playback)

    def test_each_new_listen_turn_resets_the_microphone_dma_queue(self) -> None:
        voice_source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        begin_listening = voice_source[
            voice_source.index("void VoiceController::begin_listening") :
            voice_source.index("void VoiceController::finish_listening")
        ]
        self.assertIn("audio_->reset_microphone_capture()", begin_listening)
        audio_source = AUDIO_HAL.read_text(encoding="utf-8")
        reset = audio_source[
            audio_source.index("esp_err_t AudioHal::reset_microphone_capture") :
            audio_source.index("esp_err_t AudioHal::read_microphone_frame")
        ]
        self.assertLess(
            reset.index("i2s_channel_disable(rx_channel_)"),
            reset.index("i2s_channel_enable(rx_channel_)"),
        )


if __name__ == "__main__":
    unittest.main()
