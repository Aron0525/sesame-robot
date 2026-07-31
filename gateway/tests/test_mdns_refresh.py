from __future__ import annotations

import unittest
from unittest.mock import patch

from sesame_voice_gateway.config import Settings
from sesame_voice_gateway.discovery import (
    MdnsAdvertiser,
    NonUniqueNameException,
    _resolve_advertised_ipv4,
)


class _FakeZeroconf:
    instances: list[_FakeZeroconf] = []

    def __init__(self) -> None:
        self.registered: list[object] = []
        self.updated: list[object] = []
        self.unregistered: list[object] = []
        self.closed = False
        self.instances.append(self)

    def register_service(self, service: object, **kwargs: object) -> None:
        self.registered.append((service, kwargs))

    def update_service(self, service: object) -> None:
        self.updated.append(service)

    def unregister_service(self, service: object) -> None:
        self.unregistered.append(service)

    def close(self) -> None:
        self.closed = True


class _FakeRouteProbe:
    def __enter__(self) -> _FakeRouteProbe:
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        return None

    def connect(self, address: object) -> None:
        del address

    def getsockname(self) -> tuple[str, int]:
        return ("198.18.0.1", 0)


def _settings() -> Settings:
    return Settings(
        _env_file=None,
        allow_remote_speech=True,
        dashscope_api_key="test-key",
        openclaw_token="test-token",
        openclaw_session_key_secret="test-secret",
        device_tokens={"dev_001": "device-token"},
        device_users={"dev_001": "usr_001"},
        enable_mdns=True,
    )


class MdnsRefreshTest(unittest.TestCase):
    def setUp(self) -> None:
        _FakeZeroconf.instances.clear()

    def test_refresh_republishes_when_the_automatic_lan_address_changes(self) -> None:
        advertiser = MdnsAdvertiser(_settings())
        with patch("sesame_voice_gateway.discovery.Zeroconf", _FakeZeroconf):
            with patch(
                "sesame_voice_gateway.discovery._resolve_advertised_ipv4",
                side_effect=["192.168.10.10", "192.168.10.20", "192.168.10.20"],
            ):
                advertiser.start()
                self.assertTrue(advertiser.refresh())
                self.assertFalse(advertiser.refresh())

        zeroconf = _FakeZeroconf.instances[0]
        self.assertEqual(len(zeroconf.registered), 1)
        self.assertEqual(len(zeroconf.updated), 1)
        self.assertEqual(zeroconf.updated[0].parsed_addresses(), ["192.168.10.20"])

    def test_start_recovers_from_a_stale_mdns_instance_name(self) -> None:
        class CollidingZeroconf(_FakeZeroconf):
            def register_service(self, service: object, **kwargs: object) -> None:
                self.registered.append((service, kwargs))
                if len(self.registered) == 1:
                    raise NonUniqueNameException

        advertiser = MdnsAdvertiser(_settings())
        with patch("sesame_voice_gateway.discovery.Zeroconf", CollidingZeroconf):
            with patch("sesame_voice_gateway.discovery._resolve_advertised_ipv4", return_value="192.168.88.21"):
                advertiser.start()

        zeroconf = CollidingZeroconf.instances[0]
        self.assertEqual(len(zeroconf.registered), 2)
        self.assertEqual(zeroconf.registered[1][1], {"allow_name_change": True})
        self.assertFalse(zeroconf.closed)

    def test_auto_address_prefers_hostname_lan_address_over_vpn_route(self) -> None:
        with patch(
            "sesame_voice_gateway.discovery.socket.getaddrinfo",
            return_value=[(None, None, None, None, ("192.168.88.21", 0))],
        ):
            with patch(
                "sesame_voice_gateway.discovery.socket.socket",
                return_value=_FakeRouteProbe(),
            ):
                self.assertEqual(_resolve_advertised_ipv4(_settings()), "192.168.88.21")


if __name__ == "__main__":
    unittest.main()
