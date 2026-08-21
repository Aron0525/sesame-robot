#!/usr/bin/env python3
"""Protect the physical contract for independent microphone and SPK2 buses."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = (ROOT / "components/sesame_audio/include/sesame_audio/audio_contract.h")
CONFIG = (ROOT / "components/sesame_audio/include/sesame_audio/audio_config.h")
HAL = ROOT / "components/sesame_audio/audio_hal.cpp"


def main() -> None:
    contract = CONTRACT.read_text(encoding="utf-8")
    config = CONFIG.read_text(encoding="utf-8")
    hal = HAL.read_text(encoding="utf-8")

    for expected in (
        "kMicrophoneBclkGpio = 14",
        "kMicrophoneWsGpio = 47",
        "kMicrophoneDataGpio = 48",
        "kSpeakerBclkGpio = 1",
        "kSpeakerWsGpio = 2",
        "kSpeakerDataGpio = 3",
    ):
        assert expected in contract, expected

    for expected in (
        "kMicrophoneBclkPin",
        "kMicrophoneWsPin",
        "kMicrophoneDataPin",
        "kSpeakerBclkPin",
        "kSpeakerWsPin",
        "kSpeakerDataPin",
    ):
        assert expected in config, expected

    assert "I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0" in hal
    assert "i2s_new_channel(&microphone_channel_config, nullptr, &rx_channel_)" in hal
    assert "I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1" in hal
    assert "i2s_new_channel(&speaker_channel_config, &tx_channel_, nullptr)" in hal
    assert "I2S_DATA_BIT_WIDTH_16BIT" in hal
    assert "scale_speaker_pcm16(input[frame])" in hal
    assert "gpio_set_level" not in hal
    print("independent I2S microphone/SPK2 wiring verified")


if __name__ == "__main__":
    main()
