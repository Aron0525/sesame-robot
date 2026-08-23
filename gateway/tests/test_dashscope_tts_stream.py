from __future__ import annotations

import unittest
from dataclasses import dataclass
from unittest.mock import patch

from sesame_voice_gateway.providers.dashscope import DashScopeAudioClient


@dataclass(frozen=True)
class _Chunk:
    audio_data: bytes | None
    audio_url: str | None = None


class DashScopeTtsStreamTest(unittest.TestCase):
    def setUp(self) -> None:
        self.client = DashScopeAudioClient(
            api_key="test-key",
            http_base_url="https://example.test/api/v1",
            websocket_base_url="wss://example.test/api-ws/v1/inference",
        )

    def test_skips_the_terminal_cumulative_audio_block(self) -> None:
        stream = [
            _Chunk(b"first"),
            _Chunk(b"second"),
            # The DashScope SDK emits this final cumulative copy after its
            # sentence-level chunks. It must not play twice.
            _Chunk(b"firstsecond"),
        ]
        with patch(
            "sesame_voice_gateway.providers.dashscope.HttpSpeechSynthesizer.call",
            return_value=stream,
        ):
            audio = self.client.synthesize(
                text="测试",
                model="qwen-audio-3.0-tts-flash",
                voice_id="longanhuan_v3.6",
                sample_rate=16_000,
                speed=1.0,
                instruction=None,
            )

        self.assertEqual(audio, b"firstsecond")

    def test_keeps_audio_when_stream_only_has_a_terminal_block(self) -> None:
        with patch(
            "sesame_voice_gateway.providers.dashscope.HttpSpeechSynthesizer.call",
            return_value=[_Chunk(b"terminal-audio")],
        ):
            audio = self.client.synthesize(
                text="测试",
                model="qwen-audio-3.0-tts-flash",
                voice_id="longanhuan_v3.6",
                sample_rate=16_000,
                speed=1.0,
                instruction=None,
            )

        self.assertEqual(audio, b"terminal-audio")

    def test_exposes_non_cumulative_chunks_for_immediate_pcm_framing(self) -> None:
        stream = [_Chunk(b"first"), _Chunk(b"second"), _Chunk(b"firstsecond")]
        with patch(
            "sesame_voice_gateway.providers.dashscope.HttpSpeechSynthesizer.call",
            return_value=stream,
        ):
            chunks = list(
                self.client.synthesize_chunks(
                    text="测试",
                    model="qwen-audio-3.0-tts-flash",
                    voice_id="longanhuan_v3.6",
                    sample_rate=16_000,
                    speed=1.0,
                    instruction=None,
                )
            )

        self.assertEqual(chunks, [b"first", b"second"])


if __name__ == "__main__":
    unittest.main()
