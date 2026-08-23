from __future__ import annotations

import stat
from pathlib import Path
from typing import TypedDict

import uvicorn

from sesame_voice_gateway.config import Settings, get_settings
from sesame_voice_gateway.protocol.control import MAX_CONTROL_FRAME_BYTES


class UvicornOptions(TypedDict, total=False):
    host: str
    port: int
    log_level: str
    proxy_headers: bool
    server_header: bool
    ws_max_size: int
    ssl_certfile: str
    ssl_keyfile: str


def build_uvicorn_options(settings: Settings) -> UvicornOptions:
    options = UvicornOptions(
        host=settings.host,
        port=settings.port,
        log_level="info",
        proxy_headers=False,
        server_header=False,
        # Control frames are capped at 16 KiB and audio frames are smaller.
        # Enforce the outer transport cap before FastAPI materializes a giant
        # untrusted WebSocket message in memory.
        ws_max_size=MAX_CONTROL_FRAME_BYTES,
    )
    if not settings.tls_enabled:
        return options

    certificate = _required_file(settings.tls_cert_file, "TLS certificate")
    private_key = _required_file(settings.tls_key_file, "TLS private key")
    if stat.S_IMODE(private_key.stat().st_mode) & 0o077:
        raise ValueError("TLS private key must not be group- or world-accessible")
    options["ssl_certfile"] = str(certificate)
    options["ssl_keyfile"] = str(private_key)
    return options


def _required_file(path: Path | None, label: str) -> Path:
    if path is None:
        raise ValueError(f"{label} file is not configured")
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise ValueError(f"{label} file does not exist: {resolved}")
    return resolved


def main() -> None:
    uvicorn.run("sesame_voice_gateway.app:app", **build_uvicorn_options(get_settings()))


if __name__ == "__main__":
    main()
