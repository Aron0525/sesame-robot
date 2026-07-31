from __future__ import annotations

import asyncio
import time
from collections import deque
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import Any


_SENSITIVE_DETAIL_KEYS = frozenset({"transcript", "reply_text", "agent_reply", "prompt"})
_TERMINAL_STATUSES = frozenset({"completed", "failed", "cancelled"})


@dataclass(slots=True)
class _DeviceState:
    device_id: str
    online: bool = False
    session_id: str | None = None
    current_turn_id: str | None = None
    current_stage: str = "offline"
    updated_at_ms: int = 0

    def as_dict(self) -> dict[str, Any]:
        return {
            "device_id": self.device_id,
            "online": self.online,
            "session_id": self.session_id,
            "current_turn_id": self.current_turn_id,
            "current_stage": self.current_stage,
            "updated_at_ms": self.updated_at_ms,
        }


@dataclass(slots=True)
class _TurnState:
    device_id: str
    turn_id: str
    status: str = "running"
    current_stage: str = "created"
    created_at_ms: int = 0
    updated_at_ms: int = 0
    uplink_packets: int = 0
    uplink_bytes: int = 0
    downlink_packets: int = 0
    downlink_bytes: int = 0

    def as_dict(self) -> dict[str, Any]:
        return {
            "device_id": self.device_id,
            "turn_id": self.turn_id,
            "status": self.status,
            "current_stage": self.current_stage,
            "created_at_ms": self.created_at_ms,
            "updated_at_ms": self.updated_at_ms,
            "uplink": {
                "packet_count": self.uplink_packets,
                "byte_count": self.uplink_bytes,
                "duration_ms": self.uplink_packets * 20,
            },
            "downlink": {
                "packet_count": self.downlink_packets,
                "byte_count": self.downlink_bytes,
                "duration_ms": self.downlink_packets * 20,
            },
        }


class ObservabilityStore:
    """Bounded, in-memory diagnostic state for the local Gateway dashboard.

    This deliberately stores operational metadata rather than raw audio. Text fields
    are withheld unless the local operator explicitly enables debug content.
    """

    def __init__(
        self,
        *,
        max_events: int = 500,
        expose_debug_content: bool = False,
        clock: Callable[[], int] | None = None,
    ) -> None:
        if max_events < 1:
            raise ValueError("max_events must be positive")
        self._events: deque[dict[str, Any]] = deque(maxlen=max_events)
        self._devices: dict[str, _DeviceState] = {}
        self._turns: dict[tuple[str, str], _TurnState] = {}
        self._subscribers: set[asyncio.Queue[dict[str, Any]]] = set()
        self._next_event_id = 1
        self._expose_debug_content = expose_debug_content
        self._clock = clock or (lambda: int(time.time() * 1_000))

    def device_connected(self, *, device_id: str, session_id: str) -> None:
        timestamp_ms = self._timestamp_ms()
        device = self._device(device_id, timestamp_ms)
        device.online = True
        device.session_id = session_id
        device.current_stage = "wss.connected"
        self._record(
            timestamp_ms=timestamp_ms,
            device_id=device_id,
            turn_id=None,
            stage="wss",
            status="connected",
            details={"transport": "wss"},
        )

    def device_disconnected(self, *, device_id: str, session_id: str | None = None) -> None:
        timestamp_ms = self._timestamp_ms()
        device = self._device(device_id, timestamp_ms)
        if session_id is not None and device.session_id not in {None, session_id}:
            return
        device.online = False
        device.session_id = None
        device.current_turn_id = None
        device.current_stage = "offline"
        self._record(
            timestamp_ms=timestamp_ms,
            device_id=device_id,
            turn_id=None,
            stage="wss",
            status="disconnected",
            details={"transport": "wss"},
        )

    def session_ready(self, *, device_id: str, session_id: str) -> None:
        timestamp_ms = self._timestamp_ms()
        device = self._device(device_id, timestamp_ms)
        device.online = True
        device.session_id = session_id
        device.current_stage = "ready"
        self._record(
            timestamp_ms=timestamp_ms,
            device_id=device_id,
            turn_id=None,
            stage="session",
            status="ready",
            details={"protocol": "v1", "codec": "opus", "sample_rate": 16_000},
        )

    def record_device_stage(
        self,
        *,
        device_id: str,
        stage: str,
        status: str,
        details: dict[str, Any] | None = None,
        update_current_stage: bool = True,
    ) -> None:
        """Record a hardware-side acknowledgement translated from USB serial.

        Serial firmware turns have a counter rather than the Gateway UUID turn_id,
        so this is deliberately kept as a device event instead of guessing a join.
        """
        timestamp_ms = self._timestamp_ms()
        device = self._device(device_id, timestamp_ms)
        if update_current_stage:
            device.current_stage = stage
        self._record(
            timestamp_ms=timestamp_ms,
            device_id=device_id,
            turn_id=None,
            stage=stage,
            status=status,
            details=self._sanitize_details(details or {}),
        )

    def record_stage(
        self,
        *,
        device_id: str,
        turn_id: str,
        stage: str,
        status: str,
        elapsed_ms: int | None = None,
        details: dict[str, Any] | None = None,
    ) -> None:
        timestamp_ms = self._timestamp_ms()
        device = self._device(device_id, timestamp_ms)
        turn = self._turn(device_id, turn_id, timestamp_ms)
        turn.current_stage = stage
        turn.updated_at_ms = timestamp_ms
        if status in {"failed", "cancelled"}:
            turn.status = status
        elif stage == "tts.downlink" and status == "completed":
            turn.status = "completed"

        if status in {"failed", "cancelled"}:
            device.current_stage = status
            device.current_turn_id = None
        elif stage == "tts.downlink" and status == "completed":
            device.current_stage = "ready"
            device.current_turn_id = None
        else:
            device.current_stage = stage
            device.current_turn_id = turn_id

        event_details = self._sanitize_details(details or {})
        if elapsed_ms is not None:
            event_details["elapsed_ms"] = max(0, elapsed_ms)
        self._record(
            timestamp_ms=timestamp_ms,
            device_id=device_id,
            turn_id=turn_id,
            stage=stage,
            status=status,
            details=event_details,
        )

    def record_audio_progress(
        self,
        *,
        device_id: str,
        turn_id: str,
        direction: str,
        packet_count: int,
        byte_count: int,
    ) -> None:
        if direction not in {"up", "down"}:
            raise ValueError("audio direction must be 'up' or 'down'")
        timestamp_ms = self._timestamp_ms()
        device = self._device(device_id, timestamp_ms)
        turn = self._turn(device_id, turn_id, timestamp_ms)
        packet_count = max(0, packet_count)
        byte_count = max(0, byte_count)
        if direction == "up":
            turn.uplink_packets = packet_count
            turn.uplink_bytes = byte_count
        else:
            turn.downlink_packets = packet_count
            turn.downlink_bytes = byte_count
        turn.current_stage = f"audio.{direction}"
        turn.updated_at_ms = timestamp_ms
        device.current_stage = f"audio.{direction}"
        device.current_turn_id = turn_id
        self._record(
            timestamp_ms=timestamp_ms,
            device_id=device_id,
            turn_id=turn_id,
            stage=f"audio.{direction}",
            status="progress",
            details={
                "packet_count": packet_count,
                "byte_count": byte_count,
                "duration_ms": packet_count * 20,
            },
        )

    def snapshot(self) -> dict[str, Any]:
        devices = sorted(
            (device.as_dict() for device in self._devices.values()),
            key=lambda device: device["updated_at_ms"],
            reverse=True,
        )
        turns = sorted(
            (turn.as_dict() for turn in self._turns.values()),
            key=lambda turn: turn["updated_at_ms"],
            reverse=True,
        )
        return {
            "devices": devices,
            "turns": turns[:100],
            "events": list(self._events),
            "debug_content": self._expose_debug_content,
        }

    def subscribe(self) -> asyncio.Queue[dict[str, Any]]:
        queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=128)
        self._subscribers.add(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue[dict[str, Any]]) -> None:
        self._subscribers.discard(queue)

    def _timestamp_ms(self) -> int:
        return max(0, int(self._clock()))

    def _device(self, device_id: str, timestamp_ms: int) -> _DeviceState:
        device = self._devices.get(device_id)
        if device is None:
            device = _DeviceState(device_id=device_id)
            self._devices[device_id] = device
        device.updated_at_ms = timestamp_ms
        return device

    def _turn(self, device_id: str, turn_id: str, timestamp_ms: int) -> _TurnState:
        key = (device_id, turn_id)
        turn = self._turns.get(key)
        if turn is None:
            turn = _TurnState(
                device_id=device_id,
                turn_id=turn_id,
                created_at_ms=timestamp_ms,
                updated_at_ms=timestamp_ms,
            )
            self._turns[key] = turn
        return turn

    def _record(
        self,
        *,
        timestamp_ms: int,
        device_id: str | None,
        turn_id: str | None,
        stage: str,
        status: str,
        details: dict[str, Any],
    ) -> None:
        event = {
            "id": self._next_event_id,
            "timestamp_ms": timestamp_ms,
            "device_id": device_id,
            "turn_id": turn_id,
            "stage": stage,
            "status": status,
            "details": details,
        }
        self._next_event_id += 1
        self._events.append(event)
        for queue in tuple(self._subscribers):
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    continue
            queue.put_nowait(event)

    def _sanitize_details(self, details: dict[str, Any]) -> dict[str, Any]:
        sanitized: dict[str, Any] = {}
        for key, value in details.items():
            if key in _SENSITIVE_DETAIL_KEYS and not self._expose_debug_content:
                sanitized[key] = "<hidden>"
                continue
            sanitized[key] = self._sanitize_value(value)
        return sanitized

    @staticmethod
    def _sanitize_value(value: Any) -> Any:
        if value is None or isinstance(value, bool | int | float):
            return value
        if isinstance(value, str):
            return value[:1_000]
        if isinstance(value, list):
            return [ObservabilityStore._sanitize_value(item) for item in value[:20]]
        if isinstance(value, tuple):
            return [ObservabilityStore._sanitize_value(item) for item in value[:20]]
        if isinstance(value, dict):
            return {
                str(key)[:100]: ObservabilityStore._sanitize_value(item)
                for key, item in list(value.items())[:20]
            }
        return str(value)[:1_000]
