"""Uvicorn factory for the local Sesame Gateway console."""

from __future__ import annotations

import json
import os

import uvicorn

from sesame_endpoint_gateway.app import create_app
from sesame_endpoint_gateway.discovery import DiscoveryConfig, MdnsAdvertiser


def environment_flag(name: str, *, default: bool = False) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def device_tokens_from_environment() -> dict[str, str]:
    """Load the provisioned device-token map, with single-device fallback."""
    encoded = os.environ.get("SESAME_DEVICE_TOKENS")
    if encoded:
        try:
            parsed = json.loads(encoded)
        except json.JSONDecodeError as exc:
            raise RuntimeError("SESAME_DEVICE_TOKENS must be valid JSON") from exc
        if not isinstance(parsed, dict) or not parsed or not all(
            isinstance(device_id, str)
            and bool(device_id)
            and isinstance(token, str)
            and bool(token)
            for device_id, token in parsed.items()
        ):
            raise RuntimeError("SESAME_DEVICE_TOKENS must map device IDs to non-empty tokens")
        return parsed
    return {
        os.environ["SESAME_DEVICE_ID"]: os.environ["SESAME_DEVICE_TOKEN"],
    }


def create_runtime_app():
    """Build the app from explicit runtime configuration, never embedded secrets."""
    device_tokens = device_tokens_from_environment()
    gateway_id = os.environ.get("SESAME_GATEWAY_ID", "sesame-edge")
    scene_control_token = os.environ.get("SESAME_SCENE_CONTROL_TOKEN")
    mdns_advertiser = None
    if environment_flag("SESAME_ENABLE_MDNS"):
        mdns_advertiser = MdnsAdvertiser(
            DiscoveryConfig(
                gateway_id=gateway_id,
                port=int(os.environ.get("SESAME_PORT", "8788")),
                hostname=os.environ.get("SESAME_MDNS_HOSTNAME", "sesame-gateway"),
                instance_name=os.environ.get("SESAME_MDNS_INSTANCE_NAME", "Sesame Endpoint Gateway"),
                advertised_ipv4=os.environ.get("SESAME_ADVERTISED_IPV4"),
            )
        )
    return create_app(
        device_tokens=device_tokens,
        gateway_id=gateway_id,
        scene_control_token=scene_control_token,
        mdns_advertiser=mdns_advertiser,
    )


def main() -> None:
    """Run the gateway with the TLS settings required by ESP32 mDNS clients."""
    cert_file = os.environ.get("SESAME_TLS_CERT_FILE")
    key_file = os.environ.get("SESAME_TLS_KEY_FILE")
    if not cert_file or not key_file:
        raise RuntimeError("SESAME_TLS_CERT_FILE and SESAME_TLS_KEY_FILE are required for device playback")
    uvicorn.run(
        create_runtime_app(),
        host=os.environ.get("SESAME_HOST", "0.0.0.0"),
        port=int(os.environ.get("SESAME_PORT", "8788")),
        ssl_certfile=cert_file,
        ssl_keyfile=key_file,
        log_level="info",
    )


if __name__ == "__main__":
    main()
