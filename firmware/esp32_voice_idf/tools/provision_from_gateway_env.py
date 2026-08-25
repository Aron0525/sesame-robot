#!/usr/bin/env python3
"""Build a private ESP32 NVS image from the local gateway configuration.

The temporary JSON holds the Wi-Fi password and device token only while the
standard NVS generator is running. It is deleted before this command returns.
The generated binary remains intentionally private (mode 0600).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def read_dotenv(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[key.strip()] = value
    return values


def wifi_password_from_keychain(ssid: str) -> str:
    result = subprocess.run(
        [
            "/usr/bin/security",
            "find-generic-password",
            "-D",
            "AirPort network password",
            "-a",
            ssid,
            "-w",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    password = result.stdout.rstrip("\n")
    if result.returncode != 0 or not password:
        raise RuntimeError(
            "Wi-Fi password is not available from the macOS Keychain; "
            "provide it only through this command's standard input."
        )
    return password


def password_from_stdin() -> str:
    password = sys.stdin.readline().rstrip("\r\n")
    if not password:
        raise RuntimeError("empty Wi-Fi password supplied on standard input")
    return password


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gateway-env", type=Path, required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--wifi-ssid", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--root-ca", type=Path)
    parser.add_argument(
        "--wifi-password-stdin",
        action="store_true",
        help="read one password line from stdin instead of macOS Keychain",
    )
    args = parser.parse_args()

    env_path = args.gateway_env.resolve()
    values = read_dotenv(env_path)
    try:
        device_tokens = json.loads(values["SESAME_DEVICE_TOKENS"])
    except (KeyError, json.JSONDecodeError) as error:
        raise RuntimeError("SESAME_DEVICE_TOKENS must be a JSON object") from error
    if not isinstance(device_tokens, dict):
        raise RuntimeError("SESAME_DEVICE_TOKENS must be a JSON object")
    token = device_tokens.get(args.device_id)
    if not isinstance(token, str) or not token:
        raise RuntimeError("the requested device_id has no gateway token")

    root_ca = args.root_ca or env_path.parent / ".tls" / "root-ca.pem"
    if not root_ca.is_file():
        raise RuntimeError("gateway root CA PEM does not exist")
    wifi_password = (
        password_from_stdin()
        if args.wifi_password_stdin
        else wifi_password_from_keychain(args.wifi_ssid)
    )
    private_config = {
        "wifi_ssid": args.wifi_ssid,
        "wifi_pass": wifi_password,
        "device_id": args.device_id,
        "gateway_id": values.get("SESAME_GATEWAY_ID", "gw_local_dev"),
        "device_token": token,
        "root_ca_path": str(root_ca.resolve()),
    }

    generator = Path(__file__).with_name("generate_nvs.py")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sesame-device-config-") as directory:
        config_path = Path(directory) / "device-config.json"
        config_path.write_text(json.dumps(private_config), encoding="utf-8")
        subprocess.run(
            [sys.executable, str(generator), str(config_path), str(args.output)],
            check=True,
        )
    print(f"generated private NVS image for {args.device_id}: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
