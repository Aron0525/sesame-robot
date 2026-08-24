#!/usr/bin/env python3
"""Behavior checks for the stable, targeted ESP32-S3 flash workflow."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
FLASH_SCRIPT = PROJECT_DIR / "tools" / "flash.sh"


class FlashScriptTest(unittest.TestCase):
    def run_flash(self, *arguments: str) -> list[str]:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            command_log = temp_path / "commands.log"
            fake_bin = temp_path / "bin"
            fake_bin.mkdir()
            export_script = temp_path / "export.sh"
            build_dir = temp_path / "build-codex-2"
            backup_dir = temp_path / "nvs-backups"
            port = temp_path / "esp32-port"
            nvs_image = temp_path / "device-nvs.bin"
            port.touch()
            nvs_image.write_bytes(bytes([0xFF]) * 24576)
            build_dir.mkdir()
            (build_dir / "sesame_robot_v3.bin").write_bytes(bytes([0xA5]) * 32768)

            export_script.write_text("#!/usr/bin/env bash\n", encoding="utf-8")
            for command in ("idf.py", "esptool.py"):
                (fake_bin / command).write_text(
                    "#!/usr/bin/env bash\n"
                    "printf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"$FLASH_TEST_LOG\"\n"
                    "if [[ \" $* \" == *\" read_flash \"* ]]; then touch \"${!#}\"; fi\n",
                    encoding="utf-8",
                )
                (fake_bin / command).chmod(0o755)

            environment = os.environ | {
                "IDF_EXPORT": str(export_script),
                "FLASH_TEST_LOG": str(command_log),
                "SESAME_BUILD_DIR": str(build_dir),
                "SESAME_FLASH_BACKUP_DIR": str(backup_dir),
                "PATH": f"{fake_bin}:{os.environ['PATH']}",
            }
            completed = subprocess.run(
                [
                    "bash",
                    str(FLASH_SCRIPT),
                    *[str(nvs_image) if argument == "{nvs}" else argument for argument in arguments],
                    str(port),
                ],
                cwd=PROJECT_DIR,
                env=environment,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertTrue(backup_dir.is_dir())
            self.assertEqual(len(list(backup_dir.iterdir())), 1)
            return command_log.read_text(encoding="utf-8").splitlines()

    def test_default_paths_are_worktree_safe(self) -> None:
        script = FLASH_SCRIPT.read_text(encoding="utf-8")

        self.assertIn('build_dir="${SESAME_BUILD_DIR:-build-codex-2}"', script)
        self.assertIn(
            'backup_dir="${SESAME_FLASH_BACKUP_DIR:-${HOME}/Documents/sesame robot-backups/nvs}"',
            script,
        )
        self.assertNotIn("$project_dir/flash-diagnostics/nvs-backups", script)

    def test_default_flash_is_115200_app_only_and_preserves_nvs(self) -> None:
        commands = self.run_flash()

        self.assertTrue(any(command.startswith("idf.py -B ") and command.endswith(" build") for command in commands))
        self.assertTrue(any("esptool.py --chip esp32s3" in command for command in commands))
        self.assertTrue(
            all("--no-stub" in command for command in commands if command.startswith("esptool.py "))
        )
        self.assertTrue(any("--baud 115200" in command for command in commands))
        self.assertTrue(
            any("read_flash 0x9000 0x6000" in command and "--after no_reset" in command for command in commands)
        )
        app_writes = [
            command
            for command in commands
            if command.startswith("esptool.py ")
            and "write_flash" in command
            and "--no-compress --verify" in command
        ]
        self.assertGreater(len(app_writes), 1)
        self.assertTrue(
            any(
                "write_flash" in command
                and "--before no_reset --after no_reset" in command
                and "--no-compress --verify 0x10000" in command
                for command in commands
            )
        )
        self.assertFalse(any("verify_flash" in command for command in commands))
        self.assertFalse(any("0x0" in command.split() and "write_flash" in command for command in commands))
        self.assertFalse(any("0x8000" in command.split() and "write_flash" in command for command in commands))
        self.assertFalse(any("0x310000" in command.split() and "write_flash" in command for command in commands))

    def test_nvs_option_writes_and_verifies_only_the_supplied_nvs_image(self) -> None:
        commands = self.run_flash("--nvs", "{nvs}")

        self.assertTrue(
            all("--no-stub" in command for command in commands if command.startswith("esptool.py "))
        )
        self.assertTrue(
            any("write_flash" in command and "0x9000" in command.split() and "--verify" in command for command in commands)
        )
        self.assertFalse(any("verify_flash" in command for command in commands))
        self.assertFalse(any("0x0" in command.split() and "write_flash" in command for command in commands))
        self.assertFalse(any("0x8000" in command.split() and "write_flash" in command for command in commands))
        self.assertFalse(any("0x310000" in command.split() and "write_flash" in command for command in commands))


if __name__ == "__main__":
    unittest.main()
