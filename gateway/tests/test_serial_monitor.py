from __future__ import annotations

import unittest

from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.serial_monitor import parse_firmware_log_line, publish_firmware_event


class FirmwareSerialMonitorTest(unittest.TestCase):
    def test_translates_boot_and_playback_logs_without_retaining_raw_lines(self) -> None:
        start = parse_firmware_log_line("I (101) sesame_voice: P2 BOOT start: turn=7")
        progress = parse_firmware_log_line(
            "I (142) sesame_voice: P2 uplink progress: turn=7 frames=50"
        )
        plan = parse_firmware_log_line(
            "I (900) sesame_voice: P2 response.plan accepted: generation=4 action=wave expression=happy"
        )
        playback = parse_firmware_log_line(
            "I (1200) sesame_voice: P2 playback completed: generation=4"
        )

        self.assertEqual((start.stage, start.status), ("device.listen", "started"))
        self.assertEqual(progress.details["packet_count"], 50)
        self.assertEqual(plan.details, {"generation_id": 4, "action": "wave", "expression": "happy"})
        self.assertEqual((playback.stage, playback.status), ("device.playback", "completed"))

    def test_publishes_a_structured_device_event(self) -> None:
        event = parse_firmware_log_line("I (101) sesame_voice: P2 BOOT start: turn=7")
        assert event is not None
        store = ObservabilityStore(max_events=20)

        publish_firmware_event(store, device_id="dev_001", event=event)

        snapshot = store.snapshot()
        self.assertEqual(snapshot["devices"][0]["current_stage"], "device.listen")
        self.assertEqual(snapshot["events"][0]["details"], {"firmware_turn": 7})

    def test_ignores_unmapped_firmware_log_lines(self) -> None:
        self.assertIsNone(parse_firmware_log_line("I (10) wifi: unrelated low-level log"))


if __name__ == "__main__":
    unittest.main()
