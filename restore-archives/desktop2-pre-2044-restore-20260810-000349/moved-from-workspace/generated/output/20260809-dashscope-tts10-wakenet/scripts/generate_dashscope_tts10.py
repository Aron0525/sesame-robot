#!/usr/bin/env python3
"""Generate ten reproducible DashScope TTS positives for 你好，芝麻."""
from __future__ import annotations

import hashlib
import json
import os
import wave
from pathlib import Path

from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.providers.dashscope import DashScopeAudioClient

out = Path(os.environ["WAKEWORD_OUTPUT_DIR"]).resolve()
text = "你好，芝麻"
speeds = (0.82, 0.86, 0.90, 0.94, 0.98, 1.02, 1.06, 1.10, 1.14, 1.18)
settings = Settings()
if not settings.allow_remote_speech:
    raise RuntimeError("SESAME_ALLOW_REMOTE_SPEECH must be true")
if settings.dashscope_api_key is None:
    raise RuntimeError("DashScope API key is not configured")

client = DashScopeAudioClient(
    api_key=settings.dashscope_api_key.get_secret_value(),
    http_base_url=settings.dashscope_http_base_url,
    websocket_base_url=settings.dashscope_websocket_base_url,
)
records: list[dict[str, object]] = []
for index, speed in enumerate(speeds, start=1):
    subset = "train" if index <= 8 else "holdout"
    destination = out / "tts" / subset / f"dashscope_{index:02d}_nihao_zhima.wav"
    raw = client.synthesize(
        text=text,
        model=settings.dashscope_tts_model,
        voice_id=settings.dashscope_tts_voice_id,
        sample_rate=16_000,
        speed=speed,
        instruction="请用清晰、自然、适合近场唤醒词的普通话朗读。",
    )
    if not raw or len(raw) % 2:
        raise RuntimeError(f"invalid PCM from DashScope for sample {index}: {len(raw)} bytes")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(destination), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16_000)
        wav.writeframes(raw)
    with wave.open(str(destination), "rb") as wav:
        contract = {
            "channels": wav.getnchannels(),
            "sample_width_bytes": wav.getsampwidth(),
            "sample_rate_hz": wav.getframerate(),
            "frames": wav.getnframes(),
            "duration_ms": round(1000 * wav.getnframes() / wav.getframerate(), 2),
        }
    if contract["channels"] != 1 or contract["sample_width_bytes"] != 2 or contract["sample_rate_hz"] != 16_000:
        raise RuntimeError(f"WAV contract mismatch for sample {index}: {contract}")
    records.append({
        "id": f"dashscope_{index:02d}_nihao_zhima",
        "category": "target",
        "label": 1,
        "text": text,
        "split": subset,
        "engine": "DashScope qwen-audio-3.0-tts-flash",
        "voice_id": settings.dashscope_tts_voice_id,
        "rate": speed,
        "path": str(destination),
        "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
        "format": contract,
    })
    print(json.dumps({"index": index, "split": subset, "speed": speed, "bytes": len(raw), **contract}, ensure_ascii=False), flush=True)

hashes = [str(item["sha256"]) for item in records]
if len(set(hashes)) != len(hashes):
    raise RuntimeError("DashScope produced duplicate WAV content across requested rates")
manifest = {
    "schema_version": 1,
    "purpose": "DashScope TTS ten-positive extension for 你好芝麻 WakeNet retraining",
    "training_records": 8,
    "holdout_records": 2,
    "records": records,
}
(out / "tts" / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
print(json.dumps({"manifest": str(out / "tts" / "manifest.json"), "samples": len(records), "unique_wav_sha256": len(set(hashes))}, ensure_ascii=False))
