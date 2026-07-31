from __future__ import annotations

import re
from pathlib import Path
from typing import Literal
from urllib.parse import urlparse

from pydantic import Field, SecretStr, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


_DEVICE_ID_PATTERN = re.compile(r"^[A-Za-z0-9](?:[A-Za-z0-9_-]{0,61}[A-Za-z0-9])?$")


def _canonical_device_mdns_hostname(device_id: str) -> str:
    """Match the ESP32 local-web mDNS hostname transformation."""
    return device_id.replace("_", "-").lower()


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_prefix="SESAME_",
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )

    gateway_id: str = Field(default="gw_local_dev", min_length=1, max_length=100)
    host: str = "0.0.0.0"
    port: int = Field(default=8765, ge=1, le=65_535)
    tls_enabled: bool = False
    tls_cert_file: Path | None = None
    tls_key_file: Path | None = None
    enable_mdns: bool = False
    mdns_instance_name: str = "Sesame Voice Gateway"
    mdns_hostname: str = "sesame-gateway"
    mdns_refresh_interval_seconds: int = Field(default=30, ge=5, le=3_600)
    # Only set this for a router-reserved/static address. Leaving it unset
    # lets the mDNS record follow DHCP and network changes automatically.
    advertised_ipv4: str | None = None

    device_tokens: dict[str, str] = Field(default_factory=dict)
    device_users: dict[str, str] = Field(default_factory=dict)
    conversation_ttl_seconds: int = Field(default=1_800, ge=60, le=86_400)
    conversation_cleanup_interval_seconds: int = Field(default=60, ge=10, le=3_600)
    dashboard_event_limit: int = Field(default=500, ge=50, le=5_000)
    # The dashboard is bound to loopback-only HTTP requests. Even there, user
    # text remains hidden unless a local operator deliberately enables this.
    dashboard_debug_content: bool = False
    # Optional direct USB serial observation for the ESP32 attached to this Mac.
    # The Gateway becomes the sole serial owner while this is enabled.
    serial_monitor_enabled: bool = False
    serial_monitor_port: str = "/dev/cu.usbmodem101"
    serial_monitor_baud_rate: int = Field(default=115_200, ge=9_600, le=4_000_000)
    serial_monitor_device_id: str | None = Field(default=None, min_length=1, max_length=100)

    asr_provider: Literal["dashscope"] = "dashscope"
    tts_provider: Literal["dashscope"] = "dashscope"
    provider_mode: Literal["openclaw"] = "openclaw"
    allow_remote_speech: bool = False

    dashscope_api_key: SecretStr | None = None
    dashscope_workspace_id: str | None = Field(default=None, min_length=1, max_length=100)
    dashscope_region: Literal["beijing", "singapore"] = "beijing"
    dashscope_asr_model: str = "fun-asr-realtime"
    dashscope_asr_language: str = "zh"
    dashscope_tts_model: str = "qwen-audio-3.0-tts-flash"
    dashscope_tts_voice_id: str = "longanhuan_v3.6"
    dashscope_timeout_seconds: float = Field(default=30.0, gt=0, le=120)

    openclaw_url: str = "ws://127.0.0.1:18789"
    openclaw_agent_id: str = Field(default="sesame", pattern=r"^[a-z0-9][a-z0-9_-]{0,63}$")
    openclaw_token: SecretStr | None = None
    openclaw_session_key_secret: SecretStr | None = None
    openclaw_sandbox_cleanup_enabled: bool = False
    openclaw_timeout_seconds: float = Field(default=60.0, gt=0, le=300)
    # A voice turn must fail promptly; attempts share one timeout budget.
    openclaw_max_attempts: int = Field(default=2, ge=1, le=5)
    openclaw_retry_initial_delay_seconds: float = Field(default=0.25, gt=0, le=10)
    openclaw_retry_max_delay_seconds: float = Field(default=1.0, gt=0, le=10)
    openclaw_abort_timeout_seconds: float = Field(default=3.0, gt=0, le=10)

    @model_validator(mode="after")
    def validate_provider_configuration(self) -> Settings:
        if self.tls_enabled and (self.tls_cert_file is None or self.tls_key_file is None):
            raise ValueError(
                "SESAME_TLS_CERT_FILE and SESAME_TLS_KEY_FILE are required when TLS is enabled"
            )
        uses_dashscope = self.asr_provider == "dashscope" or self.tts_provider == "dashscope"
        if uses_dashscope and self.dashscope_api_key is None:
            raise ValueError(
                "SESAME_DASHSCOPE_API_KEY is required for DashScope audio providers"
            )
        if uses_dashscope and not self.allow_remote_speech:
            raise ValueError(
                "SESAME_ALLOW_REMOTE_SPEECH=true is required before audio or TTS text "
                "may leave this computer"
            )
        if self.provider_mode == "openclaw" and (
            self.openclaw_token is None or self.openclaw_session_key_secret is None
        ):
            raise ValueError(
                "SESAME_OPENCLAW_TOKEN and SESAME_OPENCLAW_SESSION_KEY_SECRET "
                "are required in openclaw mode"
            )
        if self.serial_monitor_enabled and (
            self.serial_monitor_device_id is None
            or self.serial_monitor_device_id not in self.device_tokens
        ):
            raise ValueError(
                "SESAME_SERIAL_MONITOR_DEVICE_ID must name a configured device when "
                "serial monitoring is enabled"
            )
        canonical_device_ids: dict[str, str] = {}
        for device_id in sorted(set(self.device_tokens) | set(self.device_users)):
            if _DEVICE_ID_PATTERN.fullmatch(device_id) is None:
                raise ValueError(
                    "configured device ID must be 1-63 ASCII letters, digits, hyphens, "
                    "or underscores and cannot begin or end with a separator"
                )
            canonical_hostname = _canonical_device_mdns_hostname(device_id)
            existing_device_id = canonical_device_ids.get(canonical_hostname)
            if existing_device_id is not None and existing_device_id != device_id:
                raise ValueError(
                    "configured device IDs collide after ESP32 mDNS hostname normalization"
                )
            canonical_device_ids[canonical_hostname] = device_id
        parsed_openclaw_url = urlparse(self.openclaw_url)
        if parsed_openclaw_url.scheme not in {"ws", "wss"} or parsed_openclaw_url.hostname not in {
            "127.0.0.1",
            "::1",
            "localhost",
        }:
            raise ValueError("SESAME_OPENCLAW_URL must remain on the local loopback interface")
        return self

    @property
    def dashscope_http_base_url(self) -> str:
        if self.dashscope_workspace_id is None:
            return f"https://{self._dashscope_public_domain}/api/v1"
        return f"https://{self.dashscope_workspace_id}.{self._dashscope_workspace_domain}/api/v1"

    @property
    def dashscope_websocket_base_url(self) -> str:
        if self.dashscope_workspace_id is None:
            return f"wss://{self._dashscope_public_domain}/api-ws/v1/inference"
        return (
            f"wss://{self.dashscope_workspace_id}."
            f"{self._dashscope_workspace_domain}/api-ws/v1/inference"
        )

    @property
    def _dashscope_workspace_domain(self) -> str:
        if self.dashscope_region == "singapore":
            return "ap-southeast-1.maas.aliyuncs.com"
        return "cn-beijing.maas.aliyuncs.com"

    @property
    def _dashscope_public_domain(self) -> str:
        if self.dashscope_region == "singapore":
            return "dashscope-intl.aliyuncs.com"
        return "dashscope.aliyuncs.com"


def get_settings() -> Settings:
    return Settings()
