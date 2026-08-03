from __future__ import annotations

import asyncio
import re
from dataclasses import dataclass
from typing import Any

from sesame_voice_gateway.observability import ObservabilityStore


@dataclass(frozen=True, slots=True)
class FirmwareTraceEvent:
    stage: str
    status: str
    details: dict[str, Any]


@dataclass(frozen=True, slots=True)
class SerialPortCandidate:
    device: str
    vendor_id: int | None
    description: str


_ESPRESSIF_USB_VENDOR_ID = 0x303A
_MACOS_PSEUDO_SERIAL_PORTS = frozenset({"cu.Bluetooth-Incoming-Port", "cu.debug-console"})


_BOOT_START = re.compile(r"P2 BOOT start: turn=(?P<turn>\d+)")
_BOOT_STOP = re.compile(r"P2 BOOT stop: turn=(?P<turn>\d+) uplink_frames=(?P<frames>\d+)")
_UPLINK_PROGRESS = re.compile(r"P2 uplink progress: turn=(?P<turn>\d+) frames=(?P<frames>\d+)")
_RESPONSE_PLAN = re.compile(
    r"P2 response\.plan accepted: generation=(?P<generation>\d+) "
    r"action=(?P<action>[a-z_]+) expression=(?P<expression>[a-z_]+)"
)
_TTS_START = re.compile(r"P2 tts\.start accepted: generation=(?P<generation>\d+)")
_TTS_STOP = re.compile(r"P2 tts\.stop received: generation=(?P<generation>\d+)")
_PLAYBACK_COMPLETED = re.compile(r"P2 playback completed: generation=(?P<generation>\d+)")
_ACTION_RESULT = re.compile(r"P2 action\.result sent: status=(?P<status>[a-z_]+)")


def parse_firmware_log_line(line: str) -> FirmwareTraceEvent | None:
    """Translate only known firmware logs; raw serial output is never retained."""
    if "P1 WSS transport connected" in line:
        return FirmwareTraceEvent("device.wss", "connected", {})
    if "P1 WSS transport disconnected" in line:
        return FirmwareTraceEvent("device.wss", "disconnected", {})
    if "P1 session.ready accepted" in line:
        return FirmwareTraceEvent("device.session", "ready", {})

    match = _BOOT_START.search(line)
    if match is not None:
        return FirmwareTraceEvent(
            "device.listen", "started", {"firmware_turn": int(match["turn"])}
        )
    match = _UPLINK_PROGRESS.search(line)
    if match is not None:
        return FirmwareTraceEvent(
            "device.audio.up",
            "progress",
            {"firmware_turn": int(match["turn"]), "packet_count": int(match["frames"])},
        )
    match = _BOOT_STOP.search(line)
    if match is not None:
        return FirmwareTraceEvent(
            "device.listen",
            "completed",
            {"firmware_turn": int(match["turn"]), "packet_count": int(match["frames"])},
        )
    match = _RESPONSE_PLAN.search(line)
    if match is not None:
        return FirmwareTraceEvent(
            "device.response.plan",
            "completed",
            {
                "generation_id": int(match["generation"]),
                "action": match["action"],
                "expression": match["expression"],
            },
        )
    match = _TTS_START.search(line)
    if match is not None:
        return FirmwareTraceEvent(
            "device.playback", "started", {"generation_id": int(match["generation"])}
        )
    match = _TTS_STOP.search(line)
    if match is not None:
        return FirmwareTraceEvent(
            "device.playback", "stopping", {"generation_id": int(match["generation"])}
        )
    match = _PLAYBACK_COMPLETED.search(line)
    if match is not None:
        return FirmwareTraceEvent(
            "device.playback", "completed", {"generation_id": int(match["generation"])}
        )
    match = _ACTION_RESULT.search(line)
    if match is not None:
        return FirmwareTraceEvent("device.action", match["status"], {})

    if "gateway callback queue full" in line:
        return FirmwareTraceEvent("device.error", "failed", {"error_code": "callback_queue_full"})
    if "downlink queue overflow" in line:
        return FirmwareTraceEvent("device.error", "failed", {"error_code": "downlink_queue_overflow"})
    if "gateway reported a safe processing failure" in line:
        return FirmwareTraceEvent("device.error", "failed", {"error_code": "gateway_processing_failed"})
    return None


def publish_firmware_event(
    store: ObservabilityStore, *, device_id: str, event: FirmwareTraceEvent
) -> None:
    store.record_device_stage(
        device_id=device_id,
        stage=event.stage,
        status=event.status,
        details=event.details,
    )


class FirmwareSerialMonitor:
    """Own the USB serial port and publish translated ESP32 acknowledgements."""

    def __init__(
        self,
        *,
        port: str | None,
        baud_rate: int,
        device_id: str,
        store: ObservabilityStore,
        reconnect_seconds: float = 2.0,
    ) -> None:
        self._port = port
        self._baud_rate = baud_rate
        self._device_id = device_id
        self._store = store
        self._reconnect_seconds = reconnect_seconds

    async def run_forever(self) -> None:
        has_reported_unavailable = False
        while True:
            connection: Any | None = None
            try:
                port = await asyncio.to_thread(resolve_serial_port, self._port)
                connection = await asyncio.to_thread(
                    _open_serial_connection, port, self._baud_rate
                )
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                if not has_reported_unavailable:
                    self._store.record_device_stage(
                        device_id=self._device_id,
                        stage="serial",
                        status="unavailable",
                        details={"error_type": type(exc).__name__},
                        update_current_stage=False,
                    )
                    has_reported_unavailable = True
                await asyncio.sleep(self._reconnect_seconds)
                continue

            has_reported_unavailable = False
            self._store.record_device_stage(
                device_id=self._device_id,
                stage="serial",
                status="connected",
                details={"baud_rate": self._baud_rate, "port": port},
                update_current_stage=False,
            )
            try:
                while True:
                    raw_line = await asyncio.to_thread(connection.readline)
                    if not raw_line:
                        continue
                    event = parse_firmware_log_line(raw_line.decode("utf-8", errors="replace"))
                    if event is not None:
                        publish_firmware_event(self._store, device_id=self._device_id, event=event)
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                self._store.record_device_stage(
                    device_id=self._device_id,
                    stage="serial",
                    status="disconnected",
                    details={"error_type": type(exc).__name__},
                    update_current_stage=False,
                )
            finally:
                if connection is not None:
                    await asyncio.to_thread(connection.close)


def _open_serial_connection(port: str, baud_rate: int) -> Any:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError("pyserial is required for firmware serial monitoring") from exc

    connection = serial.Serial(port=None, baudrate=baud_rate, timeout=0.5)
    connection.port = port
    connection.dtr = False
    connection.rts = False
    connection.open()
    return connection


def resolve_serial_port(configured_port: str | None) -> str:
    """Resolve the current ESP32 USB serial endpoint without a fixed suffix."""
    if configured_port:
        return configured_port
    try:
        from serial.tools import list_ports  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError("pyserial is required for firmware serial monitoring") from exc

    candidates = tuple(
        SerialPortCandidate(
            device=port.device,
            vendor_id=port.vid,
            description=port.description or "",
        )
        for port in list_ports.comports()
        if port.device.startswith("/dev/cu.")
    )
    return choose_serial_port(None, candidates=candidates)


def choose_serial_port(
    configured_port: str | None, *, candidates: tuple[SerialPortCandidate, ...]
) -> str:
    """Choose an ESP32 endpoint, rejecting ambiguous USB serial topologies."""
    if configured_port:
        return configured_port

    candidates = tuple(
        candidate
        for candidate in candidates
        if candidate.device.rsplit("/", maxsplit=1)[-1] not in _MACOS_PSEUDO_SERIAL_PORTS
    )

    esp32_candidates = tuple(
        candidate
        for candidate in candidates
        if candidate.vendor_id == _ESPRESSIF_USB_VENDOR_ID
        or "espressif" in candidate.description.lower()
        or "esp32" in candidate.description.lower()
    )
    if len(esp32_candidates) == 1:
        return esp32_candidates[0].device
    if len(esp32_candidates) > 1:
        raise RuntimeError("multiple Espressif USB serial devices are connected")
    if len(candidates) == 1:
        return candidates[0].device
    if not candidates:
        raise RuntimeError("no USB serial device is currently connected")
    raise RuntimeError("multiple USB serial devices are connected; configure SESAME_SERIAL_MONITOR_PORT")
