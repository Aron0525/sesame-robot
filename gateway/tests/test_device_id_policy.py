from __future__ import annotations

import unittest

from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.protocol.control import ControlProtocolError, parse_control_event


def _settings(**overrides: object) -> Settings:
    values: dict[str, object] = {
        "_env_file": None,
        "device_tokens": {"dev_001": "test-token"},
        "device_users": {"dev_001": "user_001"},
        "allow_remote_speech": True,
        "dashscope_api_key": "test-key",
        "openclaw_token": "test-token",
        "openclaw_session_key_secret": "test-secret",
    }
    values.update(overrides)
    return Settings(**values)


def _hello(device_id: str) -> str:
    return f'''{{
      "v": 1,
      "type": "session.hello",
      "session_id": null,
      "turn_id": null,
      "request_id": null,
      "sequence": 0,
      "timestamp_ms": 1000,
      "payload": {{
        "device_id": "{device_id}",
        "gateway_id": "gw_test",
        "conversation_id": null,
        "protocol_version": 1,
        "audio": {{"codec": "opus", "sample_rate": 16000, "channels": 1, "frame_duration_ms": 20}}
      }}
    }}'''


class DeviceIdPolicyTest(unittest.TestCase):
    def test_current_underscore_style_id_remains_supported(self) -> None:
        settings = _settings()

        self.assertIn("dev_001", settings.device_tokens)

    def test_rejects_device_ids_that_cannot_be_made_into_an_esp32_mdns_hostname(self) -> None:
        with self.assertRaisesRegex(ValueError, "device ID"):
            _settings(
                device_tokens={"device id": "test-token"},
                device_users={"device id": "user_001"},
            )

    def test_rejects_device_ids_that_collide_after_mdns_normalization(self) -> None:
        with self.assertRaisesRegex(ValueError, "mDNS hostname"):
            _settings(
                device_tokens={"device_one": "token-a", "device-one": "token-b"},
                device_users={"device_one": "user-a", "device-one": "user-b"},
            )

    def test_protocol_rejects_a_device_id_that_firmware_cannot_publish_as_mdns(self) -> None:
        with self.assertRaises(ControlProtocolError):
            parse_control_event(_hello("device id"))

    def test_protocol_rejects_a_device_id_longer_than_one_mdns_label(self) -> None:
        with self.assertRaises(ControlProtocolError):
            parse_control_event(_hello("a" * 64))


if __name__ == "__main__":
    unittest.main()
