from __future__ import annotations

import json
from importlib.resources import files
from pathlib import Path
from typing import Any

SCHEMA_NAMES = frozenset(
    {
        "agent-request.v1.schema.json",
        "agent-response.v1.schema.json",
        "agent-response.v2.schema.json",
        "control-event.v1.schema.json",
    }
)


def load_schema(name: str) -> dict[str, Any]:
    """Load a versioned JSON contract shipped inside the installed package."""
    if name not in SCHEMA_NAMES:
        raise ValueError(f"unknown schema resource: {name}")
    # Editable development must validate against the checked-in contract,
    # not a stale force-included copy left in an existing virtualenv.
    project_schema = Path(__file__).resolve().parents[5] / "contracts" / "schemas" / name
    resource = files("sesame_voice_gateway").joinpath("_schemas", name)
    if project_schema.is_file():
        raw_schema = project_schema.read_text(encoding="utf-8")
    elif resource.is_file():
        raw_schema = resource.read_text(encoding="utf-8")
    else:
        raise FileNotFoundError(f"schema resource is missing: {name}")
    data = json.loads(raw_schema)
    if not isinstance(data, dict):
        raise ValueError(f"schema resource must contain a JSON object: {name}")
    return data
