#!/usr/bin/env python3
"""Bridge real ESP32 serial telemetry to a local WebSocket status feed.

Supports the existing INMP441_Audio_Test firmware. It forwards only values
actually emitted on serial and leaves unreported robot modules as "未接入" in
the web UI. No mock or generated telemetry is emitted.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import hashlib
import json
import os
import re
import termios
from dataclasses import dataclass, field


LEVEL = re.compile(r"^LEVEL\|(?P<rms>-?\d+(?:\.\d+)?)\|(?P<peak>\d+)\|(?P<dbfs>-?\d+(?:\.\d+)?)\|(?P<sound>QUIET|SOUND)$")
I2S = re.compile(r"^I2S\|BCLK=(?P<bclk>\d+)\|WS=(?P<ws>\d+)\|DIN=(?P<din>\d+)\|rate=(?P<rate>\d+)")


def websocket_text_frame(message: dict) -> bytes:
    payload = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(payload) < 126:
        return bytes((0x81, len(payload))) + payload
    if len(payload) < 65536:
        return b"\x81\x7e" + len(payload).to_bytes(2, "big") + payload
    return b"\x81\x7f" + len(payload).to_bytes(8, "big") + payload


def open_serial(path: str, baud: int) -> int:
    if baud != 115200:
        raise ValueError("当前桥接程序只支持 INMP441 测试固件使用的 115200 波特率")
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = attrs[1] = attrs[3] = 0
    attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    attrs[4] = attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


@dataclass
class Telemetry:
    modules: dict = field(default_factory=dict)
    dbfs: float | None = None
    peak: int | None = None
    received: bool = False

    def payload(self) -> dict:
        payload = {"heartbeat_ms": 1000, "modules": self.modules}
        if self.dbfs is not None:
            payload["dbfs"] = self.dbfs
        if self.peak is not None:
            payload["peak"] = self.peak
        return payload

    def parse(self, line: str) -> bool:
        if line == "INMP441_TEST|boot":
            self.modules["microphone"] = {"state": "测试固件已启动", "value": "等待 I2S", "health": "warn"}
            self.received = True
            return True
        if match := I2S.match(line):
            self.modules["microphone"] = {
                "state": "I2S 已就绪",
                "value": f"{match.group('rate')} Hz · IO{match.group('din')}",
                "health": "ok",
            }
            self.received = True
            return True
        if match := LEVEL.match(line):
            self.dbfs = float(match.group("dbfs"))
            self.peak = int(match.group("peak"))
            self.modules["microphone"] = {
                "state": "采音中" if match.group("sound") == "SOUND" else "安静监听",
                "value": f"{self.dbfs:.1f} dBFS",
                "health": "ok",
            }
            self.received = True
            return True
        if line.startswith(("ERR|", "FATAL|")):
            self.modules["microphone"] = {"state": "错误", "value": line[:512], "health": "error"}
            self.received = True
            return True
        return False


class SerialWebSocketBridge:
    def __init__(self, serial_fd: int) -> None:
        self.serial_fd = serial_fd
        self.telemetry = Telemetry()
        self.clients: set[asyncio.StreamWriter] = set()
        self.buffer = b""

    async def send(self, writer: asyncio.StreamWriter, payload: dict) -> None:
        try:
            writer.write(websocket_text_frame(payload))
            await writer.drain()
        except (ConnectionError, asyncio.IncompleteReadError):
            self.clients.discard(writer)

    async def broadcast_status(self) -> None:
        message = self.telemetry.payload()
        await asyncio.gather(*(self.send(client, message) for client in tuple(self.clients)), return_exceptions=True)

    def on_serial_readable(self) -> None:
        try:
            chunk = os.read(self.serial_fd, 4096)
        except BlockingIOError:
            return
        if not chunk:
            return
        self.buffer += chunk
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            if self.telemetry.parse(raw.decode("utf-8", errors="replace").strip("\r")):
                asyncio.create_task(self.broadcast_status())

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            request = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=5)
            headers = request.decode("latin-1").split("\r\n")
            parts = headers[0].split()
            fields = {line.split(":", 1)[0].lower(): line.split(":", 1)[1].strip() for line in headers[1:] if ":" in line}
            key = fields.get("sec-websocket-key")
            valid = len(parts) >= 2 and parts[0] == "GET" and parts[1] == "/status" and fields.get("upgrade", "").lower() == "websocket" and key
            if not valid:
                writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
                await writer.drain()
                return
            accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
            writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())
            await writer.drain()
            self.clients.add(writer)
            if self.telemetry.received:
                await self.send(writer, self.telemetry.payload())
            await reader.read()
        except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError):
            pass
        finally:
            self.clients.discard(writer)
            writer.close()
            await writer.wait_closed()


async def run(port: str, baud: int, websocket_port: int) -> None:
    serial_fd = open_serial(port, baud)
    bridge = SerialWebSocketBridge(serial_fd)
    loop = asyncio.get_running_loop()
    loop.add_reader(serial_fd, bridge.on_serial_readable)
    server = await asyncio.start_server(bridge.handle_client, "127.0.0.1", websocket_port)
    print(f"Bridge ready: {port} @ {baud} -> ws://127.0.0.1:{websocket_port}/status", flush=True)
    try:
        async with server:
            await server.serve_forever()
    finally:
        loop.remove_reader(serial_fd)
        os.close(serial_fd)


def main() -> None:
    parser = argparse.ArgumentParser(description="Forward ESP32 serial telemetry to local WebSocket clients.")
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--ws-port", type=int, default=8765)
    args = parser.parse_args()
    asyncio.run(run(args.port, args.baud, args.ws_port))


if __name__ == "__main__":
    main()
