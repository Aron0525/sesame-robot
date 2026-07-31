"""Privacy boundaries for the voice gateway.

Audio and text must remain in memory only, must never enter logs, and may only
leave the computer through explicitly approved encrypted provider endpoints.
"""

from __future__ import annotations

from urllib.parse import urlparse

MAX_REMOTE_AUDIO_SECONDS = 30
MAX_REMOTE_PCM_BYTES = 16_000 * 1 * 2 * MAX_REMOTE_AUDIO_SECONDS
MAX_REMOTE_TTS_CHARS = 4_000


class PrivacyPolicyViolation(ValueError):
    """Raised before protected voice data reaches an unapproved boundary."""


def require_remote_speech_consent(enabled: bool) -> None:
    if not enabled:
        raise PrivacyPolicyViolation(
            "remote ASR/TTS is disabled; set SESAME_ALLOW_REMOTE_SPEECH=true only "
            "after accepting that the configured provider receives audio and TTS text"
        )


def require_secure_provider_url(value: str, *, scheme: str) -> None:
    parsed = urlparse(value)
    if (
        parsed.scheme != scheme
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
    ):
        raise PrivacyPolicyViolation(f"provider URL must be a credential-free {scheme} URL")


def validate_remote_pcm(pcm: bytes) -> None:
    if not pcm:
        raise PrivacyPolicyViolation("remote ASR requires PCM audio")
    if len(pcm) > MAX_REMOTE_PCM_BYTES:
        raise PrivacyPolicyViolation("remote ASR audio exceeds the 30-second privacy limit")


def validate_remote_tts_text(text: str) -> None:
    if not text.strip():
        raise PrivacyPolicyViolation("remote TTS requires non-empty text")
    if len(text) > MAX_REMOTE_TTS_CHARS:
        raise PrivacyPolicyViolation("remote TTS text exceeds the privacy limit")
