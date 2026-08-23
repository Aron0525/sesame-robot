"""mDNS publication for ESP32 gateways configured without a fixed URL."""

from __future__ import annotations

import ipaddress
import logging
import socket
from dataclasses import dataclass
from typing import Any, Callable, Optional


# This is the service name used by the working streaming-lab firmware.
SERVICE_TYPE = "_sesame-streamgw._tcp.local."
LOGGER = logging.getLogger(__name__)


class DiscoveryError(RuntimeError):
    """The Gateway cannot publish a usable LAN endpoint."""


@dataclass(frozen=True)
class DiscoveryConfig:
    gateway_id: str
    port: int
    hostname: str
    instance_name: str
    advertised_ipv4: Optional[str] = None


def resolve_advertised_ipv4(config: DiscoveryConfig) -> str:
    if config.advertised_ipv4 is not None:
        address = ipaddress.ip_address(config.advertised_ipv4)
        if address.version != 4 or address.is_loopback or address.is_link_local:
            raise DiscoveryError("advertised IPv4 must be a LAN IPv4 address")
        return str(address)

    candidates: list[str] = []
    try:
        candidates.extend(item[4][0] for item in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET))
    except socket.gaierror:
        pass
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


class MdnsAdvertiser:
    def __init__(
        self,
        config: DiscoveryConfig,
        *,
        zeroconf_factory: Optional[Callable[[], Any]] = None,
        service_info_factory: Optional[Callable[..., Any]] = None,
    ) -> None:
        self._config = config
        self._zeroconf_factory = zeroconf_factory
        self._service_info_factory = service_info_factory
        self._zeroconf: Any = None
        self._service_info: Any = None

    def start(self) -> None:
        if self._zeroconf is not None:
            return
        service_info, zeroconf = self._build_service_info(), self._new_zeroconf()
        try:
            zeroconf.register_service(service_info)
        except Exception as exc:
            if exc.__class__.__name__ != "NonUniqueNameException":
                zeroconf.close()
                raise
            zeroconf.register_service(service_info, allow_name_change=True)
        self._zeroconf = zeroconf
        self._service_info = service_info
        LOGGER.info("published mDNS service on port %s", self._config.port)

    def stop(self) -> None:
        if self._zeroconf is None:
            return
        if self._service_info is not None:
            self._zeroconf.unregister_service(self._service_info)
        self._zeroconf.close()
        LOGGER.info("withdrawn mDNS service")
        self._zeroconf = None
        self._service_info = None

    def _build_service_info(self) -> Any:
        factory = self._service_info_factory
        if factory is None:
            try:
                from zeroconf import ServiceInfo
            except ModuleNotFoundError as exc:
                raise DiscoveryError("zeroconf is not installed") from exc
            factory = ServiceInfo
        address = resolve_advertised_ipv4(self._config)
        return factory(
            type_=SERVICE_TYPE,
            name=f"{self._config.instance_name}.{SERVICE_TYPE}",
            addresses=[socket.inet_aton(address)],
            port=self._config.port,
            properties={
                b"gateway_id": self._config.gateway_id.encode(),
                b"protocol": b"1",
                b"tls": b"1",
                b"path": b"/v2/device-stream",
            },
            server=f"{self._config.hostname.rstrip('.')}.local.",
        )

    def _new_zeroconf(self) -> Any:
        if self._zeroconf_factory is not None:
            return self._zeroconf_factory()
        try:
            from zeroconf import Zeroconf
        except ModuleNotFoundError as exc:
            raise DiscoveryError("zeroconf is not installed") from exc
        return Zeroconf()
