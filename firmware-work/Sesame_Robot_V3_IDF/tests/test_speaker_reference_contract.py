from __future__ import annotations

import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
AUDIO_HAL_HEADER = (
    PROJECT_ROOT
    / "components"
    / "sesame_audio"
    / "include"
    / "sesame_audio"
    / "audio_hal.h"
)
AUDIO_HAL_SOURCE = (
    PROJECT_ROOT / "components" / "sesame_audio" / "audio_hal.cpp"
)
VOICE_CONTROLLER = (
    PROJECT_ROOT / "components" / "sesame_voice" / "voice_controller.cpp"
)


class SpeakerReferenceContractTest(unittest.TestCase):
    def test_tx_stays_ready_until_valid_audio_has_been_preloaded(self) -> None:
        header = AUDIO_HAL_HEADER.read_text(encoding="utf-8")
        source = AUDIO_HAL_SOURCE.read_text(encoding="utf-8")
        initialize = source[
            source.index("esp_err_t AudioHal::initialize()") :
            source.index("esp_err_t AudioHal::shutdown()")
        ]

        self.assertIn("preload_speaker_frame", header)
        self.assertIn("start_speaker", header)
        self.assertIn("stop_speaker", header)
        self.assertIn("discard_speaker_preload", header)
        self.assertNotIn("i2s_channel_enable(tx_channel_)", initialize)
        self.assertIn("i2s_channel_preload_data", source)
        self.assertIn("i2s_channel_enable(tx_channel_)", source)
        self.assertIn("i2s_channel_disable(tx_channel_)", source)

    def test_playback_preloads_dma_before_start_and_stops_after_tail(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        playback = source[
            source.index("void VoiceController::run_playback()") :
            source.index("void VoiceController::handle_button")
        ]
        self.assertIn("audio_->preload_speaker_frame", playback)
        self.assertIn("audio_->start_speaker()", playback)
        self.assertIn("audio_->stop_speaker()", playback)
        preload = playback.index("audio_->preload_speaker_frame")
        start = playback.index("audio_->start_speaker()")
        tail = playback.index("vTaskDelay(pdMS_TO_TICKS(160))")
        stop = playback.index("audio_->stop_speaker()", tail)

        self.assertLess(preload, start)
        self.assertNotIn("playback_busy_ = false", playback[preload:start])
        self.assertLess(tail, stop)
        self.assertNotIn("write_speaker_frame(silence", playback[tail:stop])

    def test_failed_or_interrupted_preload_cannot_leak_into_next_turn(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        playback = source[
            source.index("void VoiceController::run_playback()") :
            source.index("void VoiceController::handle_button")
        ]
        failure = playback[
            playback.index("if (!preload_ok ||") :
            playback.index("continue;", playback.index("if (!preload_ok ||"))
        ]

        self.assertIn("audio_->discard_speaker_preload()", failure)

    def test_playback_uses_an_internal_ram_task_and_reports_tts_start(self) -> None:
        source = VOICE_CONTROLLER.read_text(encoding="utf-8")
        startup = source[
            source.index("esp_err_t VoiceController::start()") : source.index(
                "void VoiceController::stop()"
            )
        ]
        playback_entry = source[
            source.index("void VoiceController::playback_task_entry") : source.index(
                "void VoiceController::outbound_task_entry"
            )
        ]
        begin_tts = source[
            source.index("void VoiceController::begin_tts") : source.index(
                "void VoiceController::finish_tts"
            )
        ]

        self.assertIn("xTaskCreatePinnedToCore(\n          playback_task_entry", startup)
        self.assertNotIn("xTaskCreatePinnedToCoreWithCaps(\n          playback_task_entry", startup)
        self.assertIn("kPlaybackTaskStackBytes", startup)
        self.assertIn("vTaskDelete(nullptr)", playback_entry)

        active = begin_tts.index("tts_active_ = true")
        initial_stats = begin_tts.index("queue_playback_stats(false)")
        self.assertLess(active, initial_stats)


if __name__ == "__main__":
    unittest.main()
