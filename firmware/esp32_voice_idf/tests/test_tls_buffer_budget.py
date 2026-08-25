#!/usr/bin/env python3
"""Keep TLS setup below the measured contiguous internal-RAM budget."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]


def read_value(path: Path, key: str) -> int:
    prefix = f"{key}="
    for line in path.read_text().splitlines():
        if line.startswith(prefix):
            return int(line.removeprefix(prefix))
    raise AssertionError(f"{key} is not configured in {path}")


for name in ("sdkconfig.defaults", "sdkconfig"):
    value = read_value(root / name, "CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN")
    assert value <= 4096, f"{name} TLS input buffer exceeds safe 4 KiB budget: {value}"
