import sys
import unittest
from pathlib import Path

from fastapi.testclient import TestClient

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sesame_endpoint_gateway.discovery import DiscoveryConfig, MdnsAdvertiser
from sesame_endpoint_gateway.app import create_app


class FakeServiceInfo:
    def __init__(self, **kwargs) -> None:
        self.kwargs = kwargs


class FakeZeroconf:
    def __init__(self) -> None:
        self.registered = None
        self.unregistered = None
        self.closed = False

    def register_service(self, service, allow_name_change=False) -> None:
        self.registered = (service, allow_name_change)

    def unregister_service(self, service) -> None:
        self.unregistered = service

    def close(self) -> None:
        self.closed = True


class MdnsDiscoveryTests(unittest.TestCase):
    def test_advertises_the_authenticated_tls_device_stream(self) -> None:
        fake_zeroconf = FakeZeroconf()
        advertiser = MdnsAdvertiser(
            DiscoveryConfig(
                gateway_id="gateway-main",
                port=8765,
                hostname="sesame-gateway",
                instance_name="Sesame Gateway",
                advertised_ipv4="192.168.88.20",
            ),
            zeroconf_factory=lambda: fake_zeroconf,
            service_info_factory=FakeServiceInfo,
        )

        advertiser.start()
        service, allow_name_change = fake_zeroconf.registered

        self.assertFalse(allow_name_change)
        self.assertEqual(service.kwargs["type_"], "_sesame-streamgw._tcp.local.")
        self.assertEqual(service.kwargs["port"], 8765)
        self.assertEqual(service.kwargs["properties"], {
            b"gateway_id": b"gateway-main",
            b"protocol": b"1",
            b"tls": b"1",
            b"path": b"/v2/device-stream",
        })
        self.assertEqual(service.kwargs["addresses"], [b"\xc0\xa8X\x14"])

        advertiser.stop()
        self.assertIs(fake_zeroconf.unregistered, service)
        self.assertTrue(fake_zeroconf.closed)

    def test_gateway_lifecycle_starts_and_stops_its_mdns_advertisement(self) -> None:
        class RecordingAdvertiser:
            def __init__(self) -> None:
                self.started = False
                self.stopped = False

            def start(self) -> None:
                self.started = True

            def stop(self) -> None:
                self.stopped = True

        advertiser = RecordingAdvertiser()
        app = create_app(
            device_tokens={"sesame-v3-001": "test-device-token"},
            mdns_advertiser=advertiser,
        )
        with TestClient(app) as client:
            self.assertTrue(advertiser.started)
            self.assertEqual(client.get("/healthz").status_code, 200)
        self.assertTrue(advertiser.stopped)


if __name__ == "__main__":
    unittest.main()
