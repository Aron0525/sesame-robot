from __future__ import annotations

from collections.abc import Callable
from typing import Any, Protocol
from urllib.parse import urlparse

import requests


class HttpResponseProtocol(Protocol):
    def raise_for_status(self) -> None: ...

    def json(self) -> dict[str, Any]: ...


PostCallable = Callable[..., HttpResponseProtocol]


class DashScopeVoiceEnrollmentClient:
    def __init__(
        self,
        *,
        api_key: str,
        http_base_url: str,
        timeout_seconds: float = 30.0,
        post: PostCallable = requests.post,
    ) -> None:
        if not api_key:
            raise ValueError("DashScope API key must not be empty")
        self._api_key = api_key
        self._endpoint = (
            f"{http_base_url.rstrip('/')}/services/audio/tts/customization"
        )
        self._timeout_seconds = timeout_seconds
        self._post = post

    def create_voice(
        self,
        *,
        audio_url: str,
        prefix: str,
        target_model: str,
    ) -> str:
        parsed_url = urlparse(audio_url)
        if parsed_url.scheme != "https" or not parsed_url.netloc:
            raise ValueError("voice reference audio must use a public HTTPS URL")
        if not prefix.strip():
            raise ValueError("voice prefix must not be empty")

        response = self._post(
            self._endpoint,
            headers={
                "Authorization": f"Bearer {self._api_key}",
                "Content-Type": "application/json",
            },
            json={
                "model": "voice-enrollment",
                "input": {
                    "action": "create_voice",
                    "target_model": target_model,
                    "prefix": prefix,
                    "url": audio_url,
                },
            },
            timeout=self._timeout_seconds,
        )
        response.raise_for_status()
        payload = response.json()
        output = payload.get("output")
        voice_id = output.get("voice_id") if isinstance(output, dict) else None
        if not isinstance(voice_id, str) or not voice_id:
            raise RuntimeError("DashScope voice enrollment response has no voice_id")
        return voice_id
