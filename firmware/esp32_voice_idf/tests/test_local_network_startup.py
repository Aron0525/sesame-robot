#!/usr/bin/env python3
"""Guard the recovery network from being coupled to WSS startup again."""

from pathlib import Path


root = Path(__file__).resolve().parents[1]
main_source = (root / "main" / "app_main.cpp").read_text()
network_source = root / "components" / "sesame_transport" / "local_network.cpp"

assert network_source.exists(), "LocalNetwork must own AP/STA/mDNS startup"
assert '#include "sesame_transport/local_network.h"' in main_source
assert "local_network.start(" in main_source
assert main_source.index("local_network.start(") < main_source.index("web_control.start()")
assert main_source.index("local_network.start(") < main_source.index("voice.start()")

network_text = network_source.read_text()
assert "WIFI_MODE_STA" in network_text
assert "WIFI_MODE_AP" not in network_text
assert "kDirectControlApSsid" not in network_text
