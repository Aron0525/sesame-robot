from __future__ import annotations

import argparse
from collections.abc import Callable, Sequence
from typing import Protocol

from sesame_voice_gateway.config import Settings, get_settings
from sesame_voice_gateway.providers.voice_enrollment import DashScopeVoiceEnrollmentClient


class VoiceEnrollmentClientProtocol(Protocol):
    def create_voice(
        self,
        *,
        audio_url: str,
        prefix: str,
        target_model: str,
    ) -> str: ...


ClientFactory = Callable[..., VoiceEnrollmentClientProtocol]


def enroll_voice_from_settings(
    settings: Settings,
    *,
    audio_url: str,
    prefix: str,
    client_factory: ClientFactory = DashScopeVoiceEnrollmentClient,
) -> str:
    if settings.dashscope_api_key is None:
        raise ValueError("SESAME_DASHSCOPE_API_KEY is required")
    client = client_factory(
        api_key=settings.dashscope_api_key.get_secret_value(),
        http_base_url=settings.dashscope_http_base_url,
        timeout_seconds=settings.dashscope_timeout_seconds,
    )
    return client.create_voice(
        audio_url=audio_url,
        prefix=prefix,
        target_model=settings.dashscope_tts_model,
    )


def main(argv: Sequence[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="Create a Qwen-Audio-TTS cloned voice from an HTTPS audio URL."
    )
    parser.add_argument("--audio-url", required=True, help="Public HTTPS URL of reference audio")
    parser.add_argument("--prefix", default="sesame", help="Custom voice name prefix")
    args = parser.parse_args(argv)

    voice_id = enroll_voice_from_settings(
        get_settings(),
        audio_url=args.audio_url,
        prefix=args.prefix,
    )
    print(voice_id)


if __name__ == "__main__":
    main()
