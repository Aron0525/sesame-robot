"""Minimal stdio MCP bridge for OpenClaw scene selection.

The bridge runs as a host-side MCP process.  The Sesame agent receives only
three typed tools; it never receives the HTTP bearer token or gateway endpoint.
"""

from __future__ import annotations

import json
import os
import sys
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

GATEWAY_URL = os.environ.get("SESAME_SCENE_GATEWAY_URL", "http://127.0.0.1:8788").rstrip("/")
CONTROL_TOKEN = os.environ.get("SESAME_SCENE_CONTROL_TOKEN", "")
DEFAULT_DEVICE_ID = os.environ.get("SESAME_DEFAULT_DEVICE_ID", "")

TOOLS = [
    {
        "name": "list_scenes",
        "description": "List the three selectable Sesame scenes and their dedicated Agent and future knowledge-base mapping.",
        "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "get_scene",
        "description": "Read the active normal mode or scene profile for one Sesame robot device.",
        "inputSchema": {
            "type": "object",
            "properties": {"device_id": {"type": "string", "minLength": 1, "description": "Optional when the single-device default is configured."}},
            "additionalProperties": False,
        },
    },
    {
        "name": "select_scene",
        "description": "Switch one Sesame robot to learning, children, or work scene. Use only when the user explicitly requests a scene switch.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "device_id": {"type": "string", "minLength": 1, "description": "Optional when the single-device default is configured."},
                "scene_id": {"type": "string", "enum": ["learning", "children", "work"]},
            },
            "required": ["scene_id"],
            "additionalProperties": False,
        },
    },
]


def gateway_request(method: str, path: str, body: dict[str, Any] | None = None) -> dict[str, Any]:
    if not CONTROL_TOKEN:
        raise RuntimeError("SESAME_SCENE_CONTROL_TOKEN is not configured")
    payload = None if body is None else json.dumps(body).encode("utf-8")
    request = Request(
        f"{GATEWAY_URL}{path}",
        data=payload,
        method=method,
        headers={
            "Authorization": f"Bearer {CONTROL_TOKEN}",
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    try:
        with urlopen(request, timeout=5) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as exc:
        details = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"gateway_http_{exc.code}:{details}") from exc
    except URLError as exc:
        raise RuntimeError(f"gateway_unavailable:{exc.reason}") from exc


def device_id(arguments: dict[str, Any]) -> str:
    value = arguments.get("device_id") or DEFAULT_DEVICE_ID
    if not isinstance(value, str) or not value:
        raise ValueError("device_id is required when SESAME_DEFAULT_DEVICE_ID is not configured")
    return value


def result(request_id: Any, value: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": request_id, "result": {"content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False)}]}}


def handle(request: dict[str, Any]) -> dict[str, Any]:
    method = request.get("method")
    request_id = request.get("id")
    if method == "initialize":
        return {
            "jsonrpc": "2.0",
            "id": request_id,
            "result": {
                "protocolVersion": "2025-06-18",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "sesame-scene", "version": "1.0.0"},
            },
        }
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": request_id, "result": {"tools": TOOLS}}
    if method == "tools/call":
        params = request.get("params", {})
        name = params.get("name")
        arguments = params.get("arguments", {})
        if name == "list_scenes":
            return result(request_id, gateway_request("GET", "/v1/openclaw/scenes"))
        if name == "get_scene":
            return result(request_id, gateway_request("GET", f"/v1/openclaw/scene/{device_id(arguments)}"))
        if name == "select_scene":
            return result(request_id, gateway_request("POST", "/v1/openclaw/scene", {"device_id": device_id(arguments), "scene_id": arguments["scene_id"]}))
        raise ValueError("unknown tool")
    return {"jsonrpc": "2.0", "id": request_id, "result": {}}


def main() -> None:
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            outgoing = handle(json.loads(line))
        except Exception as exc:
            outgoing = {"jsonrpc": "2.0", "id": None, "error": {"code": -32603, "message": str(exc)}}
        sys.stdout.write(json.dumps(outgoing, separators=(",", ":"), ensure_ascii=False) + "\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
