#!/usr/bin/env python3
"""Generate the per-device NVS image without putting secrets in source code."""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import tempfile
from pathlib import Path


REQUIRED_TEXT_FIELDS = (
    "wifi_ssid",
    "wifi_pass",
    "device_id",
    "gateway_id",
    "device_token",
)

# A missing gateway_url deliberately selects the normal, portable deployment
# mode: discover the TLS gateway via _sesame-streamgw._tcp.local.  These keys
# remain supported only for a deliberate fixed-endpoint override.
OPTIONAL_TEXT_FIELDS = (
    "gateway_url",
    "gateway_tls",
)


def load_config(path: Path) -> dict[str, str]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError("device config must be a JSON object")

    config: dict[str, str] = {}
    for field in REQUIRED_TEXT_FIELDS:
        value = raw.get(field)
        if not isinstance(value, str) or not value:
            raise ValueError(f"{field} must be a non-empty string")
        config[field] = value

    for field in OPTIONAL_TEXT_FIELDS:
        value = raw.get(field)
        if value is None:
            continue
        if not isinstance(value, str) or not value:
            raise ValueError(f"{field} must be a non-empty string when provided")
        config[field] = value

    root_ca_path = raw.get("root_ca_path")
    if not isinstance(root_ca_path, str) or not root_ca_path:
        raise ValueError("root_ca_path must point to the private gateway CA PEM")
    ca_path = Path(root_ca_path).expanduser()
    if not ca_path.is_absolute():
        ca_path = path.parent / ca_path
    root_ca = ca_path.read_text(encoding="utf-8")
    if "BEGIN CERTIFICATE" not in root_ca or "END CERTIFICATE" not in root_ca:
        raise ValueError("root_ca_path does not contain a PEM certificate")
    config["root_ca"] = root_ca

    conversation = raw.get("conversation")
    if conversation is not None:
        if not isinstance(conversation, str) or len(conversation) > 100:
            raise ValueError("conversation must be a string of at most 100 chars")
        config["conversation"] = conversation
    return config


def write_csv(config: dict[str, str], destination: Path) -> None:
    with destination.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(("key", "type", "encoding", "value"))
        writer.writerow(("sesame", "namespace", "", ""))
        for key, value in config.items():
            writer.writerow((key, "data", "string", value))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("config", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    config = load_config(args.config.resolve())
    if args.validate_only:
        print("device config is valid")
        return 0

    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise RuntimeError("IDF_PATH is not set; source the ESP-IDF export.sh first")
    generator = (
        Path(idf_path)
        / "components"
        / "nvs_flash"
        / "nvs_partition_generator"
        / "nvs_partition_gen.py"
    )
    if not generator.is_file():
        raise RuntimeError(f"NVS generator not found: {generator}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sesame-nvs-") as temp_dir:
        csv_path = Path(temp_dir) / "device.csv"
        write_csv(config, csv_path)
        subprocess.run(
            [
                os.environ.get("PYTHON", "python3"),
                str(generator),
                "generate",
                str(csv_path),
                str(args.output.resolve()),
                "0x6000",
            ],
            check=True,
        )
    os.chmod(args.output, 0o600)
    print(f"generated device NVS image: {args.output}")
    print("flash offset: 0x9000")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
