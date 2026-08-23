#!/usr/bin/env python3
"""Preserve the user-selected STA power-saving policy."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / "components/sesame_transport/gateway_client.cpp"


def main() -> None:
    source = CLIENT.read_text(encoding="utf-8")
    connect_wifi = source[source.index("esp_err_t GatewayClient::connect_wifi"):]
    start = connect_wifi.index("result = esp_wifi_start();")
    connect = connect_wifi.index("result = esp_wifi_connect();")
    assert start < connect
    assert "esp_wifi_set_ps(WIFI_PS_NONE)" not in connect_wifi
    print("Wi-Fi policy verified: STA power saving preserved")


if __name__ == "__main__":
    main()
