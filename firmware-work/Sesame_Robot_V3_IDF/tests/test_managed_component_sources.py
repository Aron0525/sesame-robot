"""Regression test for ESP-IDF managed-component source completeness."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import unittest


PROJECT_DIR = Path(__file__).resolve().parents[1]
REPOSITORY_DIR = PROJECT_DIR.parents[1]
ESP_NN_DIR = PROJECT_DIR / "managed_components" / "espressif__esp-nn"


class ManagedComponentSourcesTest(unittest.TestCase):
    def test_esp_nn_assembly_sources_are_present_and_tracked(self) -> None:
        cmake_text = (ESP_NN_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
        source_paths = sorted(set(re.findall(r'"(src/[^"\n]+\.S)"', cmake_text)))
        self.assertGreater(len(source_paths), 0)

        for source_path in source_paths:
            full_path = ESP_NN_DIR / source_path
            self.assertTrue(full_path.is_file(), f"missing managed source: {source_path}")
            relative_path = full_path.relative_to(REPOSITORY_DIR)
            tracked = subprocess.run(
                ["git", "-C", str(REPOSITORY_DIR), "ls-files", "--error-unmatch", str(relative_path)],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                tracked.returncode,
                0,
                f"managed source is not tracked: {relative_path}",
            )


if __name__ == "__main__":
    unittest.main()
