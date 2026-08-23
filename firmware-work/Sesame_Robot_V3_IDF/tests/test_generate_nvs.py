#!/usr/bin/env python3
"""Regression tests for method-one (mDNS) device provisioning."""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
MODULE_SPEC = importlib.util.spec_from_file_location(
    "generate_nvs", PROJECT_DIR / "tools" / "generate_nvs.py"
)
assert MODULE_SPEC is not None and MODULE_SPEC.loader is not None
generate_nvs = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(generate_nvs)


class GenerateNvsTests(unittest.TestCase):
    def test_accepts_mdns_configuration_without_fixed_gateway_url(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            ca_path = root / "gateway-ca.pem"
            ca_path.write_text(
                "-----BEGIN CERTIFICATE-----\nunit-test\n-----END CERTIFICATE-----\n",
                encoding="utf-8",
            )
            config_path = root / "device.json"
            config_path.write_text(
                json.dumps(
                    {
                        "wifi_ssid": "test-wifi",
                        "wifi_pass": "test-pass",
                        "device_id": "sesame-test-001",
                        "gateway_id": "gw_stream_lab",
                        "device_token": "test-token",
                        "root_ca_path": "gateway-ca.pem",
                    }
                ),
                encoding="utf-8",
            )

            config = generate_nvs.load_config(config_path)

        self.assertNotIn("gateway_url", config)
        self.assertNotIn("gateway_tls", config)
        self.assertEqual(config["gateway_id"], "gw_stream_lab")


if __name__ == "__main__":
    unittest.main()
