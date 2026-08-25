import pathlib
import unittest


SCRIPT_PATH = pathlib.Path(__file__).with_name("flash_sesame_robot.command")


class FlashScriptContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.script = SCRIPT_PATH.read_text(encoding="utf-8")

    def test_large_images_use_resumable_rom_loader_chunks(self) -> None:
        self.assertIn("flash_image_in_verified_chunks", self.script)
        self.assertIn(
            'flash_image_in_verified_chunks "${build_root}/${APP_IMAGE}" 0x10000 "主程序"',
            self.script,
        )
        self.assertIn(
            'flash_image_in_verified_chunks "${build_root}/srmodels/srmodels.bin"',
            self.script,
        )
        self.assertNotIn(
            'flash_verified_unit 0x410000 "${build_root}/srmodels/srmodels.bin"',
            self.script,
        )
        self.assertIn('--before "${reset_mode}" --after "${after_reset}" --no-stub', self.script)


if __name__ == "__main__":
    unittest.main()
