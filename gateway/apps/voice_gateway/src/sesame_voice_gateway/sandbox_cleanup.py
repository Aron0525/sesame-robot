from __future__ import annotations

import asyncio
from collections.abc import Awaitable, Callable
from dataclasses import dataclass

SandboxCommandRunner = Callable[[tuple[str, ...]], Awaitable[int]]


async def run_openclaw_command(command: tuple[str, ...]) -> int:
    process = await asyncio.create_subprocess_exec(
        *command,
        stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.DEVNULL,
    )
    return await process.wait()


@dataclass(frozen=True, slots=True)
class OpenClawSandboxReaper:
    run_command: SandboxCommandRunner = run_openclaw_command

    async def remove(self, session_key: str) -> None:
        if not session_key.startswith("agent:"):
            raise ValueError("sandbox session key must be an OpenClaw agent key")
        exit_code = await self.run_command(
            (
                "openclaw",
                "sandbox",
                "recreate",
                "--session",
                session_key,
                "--force",
            )
        )
        if exit_code != 0:
            raise RuntimeError(f"OpenClaw sandbox cleanup failed with exit code {exit_code}")
