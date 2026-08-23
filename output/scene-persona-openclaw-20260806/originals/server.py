"""Uvicorn factory for the local Sesame Gateway console."""

from __future__ import annotations

import os

from sesame_endpoint_gateway.app import create_app


def create_runtime_app():
    """Build the app from explicit runtime configuration, never embedded secrets."""
    device_id = os.environ["SESAME_DEVICE_ID"]
    device_token = os.environ["SESAME_DEVICE_TOKEN"]
    gateway_id = os.environ.get("SESAME_GATEWAY_ID", "sesame-edge")
    return create_app(device_tokens={device_id: device_token}, gateway_id=gateway_id)
