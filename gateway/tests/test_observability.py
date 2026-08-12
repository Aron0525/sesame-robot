from __future__ import annotations

import unittest

from sesame_voice_gateway.observability import ObservabilityStore


class ObservabilityStoreTest(unittest.TestCase):
    def test_tracks_current_stage_and_redacts_content_by_default(self) -> None:
        store = ObservabilityStore(max_events=20, expose_debug_content=False, clock=lambda: 1_000)

        store.device_connected(device_id="dev_001", session_id="ses_001")
        store.record_stage(
            device_id="dev_001",
            turn_id="turn_001",
            stage="listen",
            status="started",
            details={"mode": "boot_button"},
        )
        store.record_audio_progress(
            device_id="dev_001",
            turn_id="turn_001",
            direction="up",
            packet_count=50,
            byte_count=4_000,
        )
        store.record_stage(
            device_id="dev_001",
            turn_id="turn_001",
            stage="asr",
            status="completed",
            elapsed_ms=620,
            details={"transcript": "你好，挥挥手", "transcript_chars": 6},
        )

        snapshot = store.snapshot()

        self.assertEqual(snapshot["devices"][0]["current_stage"], "asr")
        self.assertEqual(snapshot["turns"][0]["uplink"]["packet_count"], 50)
        asr_event = next(event for event in snapshot["events"] if event["stage"] == "asr")
        self.assertEqual(asr_event["details"]["transcript"], "<hidden>")
        self.assertEqual(asr_event["details"]["transcript_chars"], 6)

    def test_marks_turn_completed_after_tts_stops(self) -> None:
        store = ObservabilityStore(max_events=20, expose_debug_content=True, clock=lambda: 1_000)
        store.device_connected(device_id="dev_001", session_id="ses_001")
        store.record_stage(
            device_id="dev_001",
            turn_id="turn_001",
            stage="tts.downlink",
            status="started",
        )
        store.record_stage(
            device_id="dev_001",
            turn_id="turn_001",
            stage="tts.downlink",
            status="completed",
            elapsed_ms=1_200,
        )

        snapshot = store.snapshot()

        self.assertEqual(snapshot["devices"][0]["current_stage"], "ready")
        self.assertEqual(snapshot["turns"][0]["status"], "completed")

    def test_marks_an_active_turn_failed_when_its_device_disconnects(self) -> None:
        store = ObservabilityStore(max_events=20, expose_debug_content=True, clock=lambda: 1_000)
        store.device_connected(device_id="dev_001", session_id="ses_001")
        store.record_stage(
            device_id="dev_001",
            turn_id="turn_001",
            stage="listen",
            status="started",
        )

        store.device_disconnected(device_id="dev_001", session_id="ses_001")

        snapshot = store.snapshot()
        self.assertEqual(snapshot["turns"][0]["status"], "failed")
        disconnect = snapshot["events"][-1]
        self.assertEqual(disconnect["stage"], "wss")
        self.assertEqual(disconnect["status"], "disconnected")
        self.assertEqual(disconnect["details"]["active_turn_id"], "turn_001")


if __name__ == "__main__":
    unittest.main()
