import os
import unittest
from unittest.mock import patch

from sesame_endpoint_gateway.server import device_tokens_from_environment


class RuntimeDeviceTokenTests(unittest.TestCase):
    def test_device_token_map_takes_precedence_over_demo_credentials(self) -> None:
        environment = {
            "SESAME_DEVICE_ID": "demo-device",
            "SESAME_DEVICE_TOKEN": "demo-token",
            "SESAME_DEVICE_TOKENS": '{"real-device":"real-token"}',
        }
        with patch.dict(os.environ, environment, clear=True):
            self.assertEqual(
                device_tokens_from_environment(),
                {"real-device": "real-token"},
            )

    def test_single_device_credentials_remain_supported(self) -> None:
        environment = {
            "SESAME_DEVICE_ID": "demo-device",
            "SESAME_DEVICE_TOKEN": "demo-token",
        }
        with patch.dict(os.environ, environment, clear=True):
            self.assertEqual(
                device_tokens_from_environment(),
                {"demo-device": "demo-token"},
            )


if __name__ == "__main__":
    unittest.main()
