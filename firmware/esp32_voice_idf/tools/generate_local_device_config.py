#!/usr/bin/env python3
"""Generate an ignored compile-time ESP32 device configuration header.

The generated header contains credentials and is intentionally excluded from
version control. It uses the device token and Gateway root CA already held by
the local Gateway configuration; the Wi-Fi password is accepted only on stdin.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
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


def read_wifi_password() -> str:
    password = sys.stdin.readline().rstrip("\r\n")
    if not password:
        raise ValueError("Wi-Fi password supplied on standard input is empty")
    return password


def c_string(value: str, label: str) -> str:
    if "\x00" in value:
        raise ValueError(f"{label} must not contain NUL")
    return json.dumps(value, ensure_ascii=False)


def root_ca_macro(value: str) -> str:
    delimiter = "SESAME_LOCAL_CA"
    closing = f"){delimiter}\""
    if closing in value:
        raise ValueError("root CA cannot be represented safely in a raw C++ string")
    if not value.startswith("-----BEGIN CERTIFICATE-----"):
        raise ValueError("root CA must be a PEM certificate")
    return f'R"{delimiter}({value}){delimiter}"'


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gateway-env", type=Path, required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--wifi-ssid", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--root-ca", type=Path)
    args = parser.parse_args()

    env_path = args.gateway_env.resolve()
    values = read_dotenv(env_path)
    if values.get("SESAME_TLS_ENABLED", "").lower() != "true":
        raise ValueError("Gateway TLS must be enabled for the WSS firmware configuration")
    try:
        device_tokens = json.loads(values["SESAME_DEVICE_TOKENS"])
    except (KeyError, json.JSONDecodeError) as error:
        raise ValueError("SESAME_DEVICE_TOKENS must be a JSON object") from error
    if not isinstance(device_tokens, dict):
        raise ValueError("SESAME_DEVICE_TOKENS must be a JSON object")
    token = device_tokens.get(args.device_id)
    if not isinstance(token, str) or not token:
        raise ValueError("the requested device ID has no Gateway token")

    gateway_id = values.get("SESAME_GATEWAY_ID")
    if not gateway_id:
        raise ValueError("SESAME_GATEWAY_ID must not be empty")
    root_ca_path = (args.root_ca or env_path.parent / ".tls" / "root-ca.pem").resolve()
    if not root_ca_path.is_file():
        raise ValueError("Gateway root CA PEM does not exist")
    root_ca = root_ca_path.read_text(encoding="utf-8").strip()
    password = read_wifi_password()

    header = "\n".join(
        (
            "#pragma once",
            "",
            "// Generated locally. This file is ignored by Git; do not commit it.",
            f"#define SESAME_LOCAL_WIFI_SSID {c_string(args.wifi_ssid, 'Wi-Fi SSID')}",
            f"#define SESAME_LOCAL_WIFI_PASSWORD {c_string(password, 'Wi-Fi password')}",
            f"#define SESAME_LOCAL_DEVICE_ID {c_string(args.device_id, 'device ID')}",
            f"#define SESAME_LOCAL_GATEWAY_ID {c_string(gateway_id, 'Gateway ID')}",
            f"#define SESAME_LOCAL_DEVICE_TOKEN {c_string(token, 'device token')}",
            f"#define SESAME_LOCAL_ROOT_CA {root_ca_macro(root_ca)}",
            "",
        )
    )
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(header, encoding="utf-8")
    os.chmod(output, 0o600)
    print(f"generated private local device config for {args.device_id}: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
