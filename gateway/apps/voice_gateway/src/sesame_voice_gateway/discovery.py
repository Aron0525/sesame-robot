from __future__ import annotations

import ipaddress
import socket
import time
from dataclasses import dataclass

from zeroconf import NonUniqueNameException, ServiceInfo, Zeroconf

from sesame_voice_gateway.config import Settings

SERVICE_TYPE = "_sesame-gw._tcp.local."


class DiscoveryError(RuntimeError):
    """Raised when the gateway cannot publish a usable LAN endpoint."""


def _resolve_advertised_ipv4(settings: Settings) -> str:
    if settings.advertised_ipv4 is not None:
        address = ipaddress.ip_address(settings.advertised_ipv4)
        if address.version != 4 or address.is_loopback:
            raise DiscoveryError("advertised_ipv4 must be a non-loopback IPv4 address")
        return str(address)

    candidates: list[str] = []
    # Prefer addresses bound to the local hostname. On macOS a VPN may own the
    # default route, so a synthetic Internet probe can otherwise select its
    # tunnel address instead of the physical LAN where mDNS clients live.
    try:
        candidates.extend(
            candidate[4][0]
            for candidate in socket.getaddrinfo(
                socket.gethostname(), None, socket.AF_INET
            )
        )
    except socket.gaierror:
        pass

    # Keep a route probe only as a fallback for hosts whose hostname does not
    # resolve to their LAN interface (a common state just after DHCP changes).
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("192.0.2.1", 9))
            candidates.append(probe.getsockname()[0])
    except OSError:
        pass

    for candidate in candidates:
        address = ipaddress.ip_address(candidate)
        if not address.is_loopback and not address.is_link_local and not address.is_unspecified:
            return str(address)
    raise DiscoveryError("no LAN IPv4 address found; set SESAME_ADVERTISED_IPV4")


def build_service_info(settings: Settings) -> ServiceInfo:
    address = _resolve_advertised_ipv4(settings)
    instance = f"{settings.mdns_instance_name}.{SERVICE_TYPE}"
    hostname = f"{settings.mdns_hostname.rstrip('.')}.local."
    return ServiceInfo(
        type_=SERVICE_TYPE,
        name=instance,
        addresses=[socket.inet_aton(address)],
        port=settings.port,
        properties={
            b"gateway_id": settings.gateway_id.encode(),
            b"protocol": b"1",
            b"tls": b"1" if settings.tls_enabled else b"0",
            b"path": b"/v1/device-stream",
            b"unix_time": str(int(time.time())).encode(),
        },
        server=hostname,
    )


@dataclass(slots=True)
class MdnsAdvertiser:
    settings: Settings
    _zeroconf: Zeroconf | None = None
    _service_info: ServiceInfo | None = None

    def start(self) -> None:
        if self._zeroconf is not None:
            return
        service_info = build_service_info(self.settings)
        zeroconf = Zeroconf()
        try:
            zeroconf.register_service(service_info)
        except NonUniqueNameException:
            # A quick Gateway restart can overlap an old mDNS goodbye packet.
            # Keep the voice service available and let Zeroconf choose a
            # collision-free service instance; ESP32 discovery authenticates
            # the advertised gateway_id rather than trusting the display name.
            zeroconf.register_service(service_info, allow_name_change=True)
        except Exception:
            zeroconf.close()
            raise
        self._zeroconf = zeroconf
        self._service_info = service_info

    def refresh(self) -> bool:
        """Republish when DHCP or the active network changes the LAN address."""
        if self._zeroconf is None or self._service_info is None:
            return False
        refreshed = build_service_info(self.settings)
        if _same_service_endpoint(self._service_info, refreshed):
            return False
        self._zeroconf.update_service(refreshed)
        self._service_info = refreshed
        return True

    def stop(self) -> None:
        if self._zeroconf is None:
            return
        if self._service_info is not None:
            self._zeroconf.unregister_service(self._service_info)
        self._zeroconf.close()
        self._zeroconf = None
        self._service_info = None


def _same_service_endpoint(left: ServiceInfo, right: ServiceInfo) -> bool:
    return (
        left.addresses == right.addresses
        and left.port == right.port
        and left.server == right.server
        and left.properties == right.properties
    )
