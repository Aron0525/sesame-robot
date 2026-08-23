#!/usr/bin/env python3
"""Append a prepared Codex final response to the user's Obsidian archive."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


VAULT = Path("/Users/mac/Documents/obsidian")
ARCHIVE_DIR = VAULT / "Codex Responses"
INDEX = VAULT / "Response Log Index.md"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    args = parser.parse_args()
    payload = json.loads(args.input.read_text(encoding="utf-8"))

    date = payload["date"]
    timestamp = payload["timestamp"]
    context = payload["context"]
    request = payload["request"]
    response = payload["response"]
    if not all(isinstance(item, str) and item for item in (
        date, timestamp, context, request, response
    )):
        raise ValueError("archive fields must be non-empty strings")

    ARCHIVE_DIR.mkdir(parents=True, exist_ok=True)
    daily = ARCHIVE_DIR / f"{date}.md"
    if not daily.exists():
        daily.write_text(f"# Codex Responses — {date}\n\n", encoding="utf-8")
    with daily.open("a", encoding="utf-8") as handle:
        handle.write(
            f"## {timestamp}\n\n"
            f"- Context: {context}\n"
            f"- Request: {request}\n\n"
            f"{response.rstrip()}\n\n"
        )

    task_note = payload.get("task_note")
    task_note_path = payload.get("task_note_path")
    if task_note is not None or task_note_path is not None:
        if not isinstance(task_note, str) or not task_note:
            raise ValueError("task_note must be a non-empty string")
        if not isinstance(task_note_path, str) or not task_note_path:
            raise ValueError("task_note_path must be a non-empty string")
        note_path = (VAULT / task_note_path).resolve()
        if VAULT.resolve() not in note_path.parents:
            raise ValueError("task note must be inside the Obsidian vault")
        note_path.parent.mkdir(parents=True, exist_ok=True)
        with note_path.open("a", encoding="utf-8") as handle:
            handle.write(f"\n{task_note.rstrip()}\n")

    entry = f"- [[Codex Responses/{date}|{date}]]"
    if INDEX.exists():
        index_text = INDEX.read_text(encoding="utf-8")
    else:
        index_text = "# Response Log Index\n\n"
    if entry not in index_text:
        with INDEX.open("a", encoding="utf-8") as handle:
            if not index_text.endswith("\n"):
                handle.write("\n")
            handle.write(f"{entry}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
