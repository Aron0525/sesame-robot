from __future__ import annotations

import unittest

from sesame_voice_gateway.observability import ObservabilityStore
from sesame_voice_gateway.serial_monitor import (
    SerialPortCandidate,
    choose_serial_port,
    parse_firmware_log_line,
    publish_firmware_event,
)


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

    def test_chooses_the_unique_espressif_port_dynamically(self) -> None:
        selected = choose_serial_port(
            None,
            candidates=(
                SerialPortCandidate("/dev/cu.usbserial-other", 0x0403, "FTDI USB Serial"),
                SerialPortCandidate("/dev/cu.usbmodem21301", 0x303A, "Espressif USB JTAG/serial"),
            ),
        )

        self.assertEqual(selected, "/dev/cu.usbmodem21301")

    def test_uses_the_only_usb_serial_port_when_board_vid_is_unknown(self) -> None:
        selected = choose_serial_port(
            None,
            candidates=(SerialPortCandidate("/dev/cu.wchusbserial1410", 0x1A86, "USB Serial"),),
        )

        self.assertEqual(selected, "/dev/cu.wchusbserial1410")

    def test_requires_an_explicit_port_when_multiple_unknown_devices_exist(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "multiple USB serial devices"):
            choose_serial_port(
                None,
                candidates=(
                    SerialPortCandidate("/dev/cu.usbserial-a", 0x0403, "FTDI USB Serial"),
                    SerialPortCandidate("/dev/cu.usbserial-b", 0x1A86, "USB Serial"),
                ),
            )

    def test_ignores_macos_builtin_pseudo_serial_ports(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "no USB serial device"):
            choose_serial_port(
                None,
                candidates=(
                    SerialPortCandidate(
                        "/dev/cu.Bluetooth-Incoming-Port", None, "Bluetooth-Incoming-Port"
                    ),
                    SerialPortCandidate("/dev/cu.debug-console", None, "debug-console"),
                ),
            )

    def test_keeps_an_explicit_port_override(self) -> None:
        self.assertEqual(
            choose_serial_port(
                "/dev/cu.usbmodem-custom",
                candidates=(SerialPortCandidate("/dev/cu.usbmodem-other", 0x303A, "Espressif"),),
            ),
            "/dev/cu.usbmodem-custom",
        )


if __name__ == "__main__":
    unittest.main()
