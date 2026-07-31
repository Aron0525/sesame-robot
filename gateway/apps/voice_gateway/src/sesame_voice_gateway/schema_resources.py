from __future__ import annotations

import json
from importlib.resources import files
from pathlib import Path
from typing import Any

SCHEMA_NAMES = frozenset(
    {
        "agent-request.v1.schema.json",
        "agent-response.v1.schema.json",
        "control-event.v1.schema.json",
    }
)


def load_schema(name: str) -> dict[str, Any]:
    """Load a versioned JSON contract shipped inside the installed package."""
    if name not in SCHEMA_NAMES:
        raise ValueError(f"unknown schema resource: {name}")
    resource = files("sesame_voice_gateway").joinpath("_schemas", name)
    if resource.is_file():
        raw_schema = resource.read_text(encoding="utf-8")
    else:
        # Editable source installs do not materialize Hatch's force-included resources.
        project_root = Path(__file__).resolve().parents[5]
        raw_schema = (project_root / "contracts" / "schemas" / name).read_text(
            encoding="utf-8"
        )
    data = json.loads(raw_schema)
    if not isinstance(data, dict):
        raise ValueError(f"schema resource must contain a JSON object: {name}")
    return data
