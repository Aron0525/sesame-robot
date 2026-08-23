import tempfile
import unittest
from pathlib import Path

from tools.motion_authoring.model import generate_motion, parse_motion_intent
from tools.motion_authoring.preview import write_preview


class MotionAuthoringTests(unittest.TestCase):
    def test_understands_chinese_swim_and_style(self):
        intent = parse_motion_intent("请让机器人快速游泳 3 次")
        self.assertEqual("dog_paddle", intent.primitive)
        self.assertEqual("energetic", intent.style)
        self.assertEqual(3, intent.cycles)

    def test_rejects_action_without_implemented_primitive(self):
        with self.assertRaisesRegex(ValueError, "只实现了游泳"):
            parse_motion_intent("让机器人踢足球")

    def test_frames_use_real_eight_servo_order_and_limits(self):
        motion = generate_motion("游泳")
        self.assertEqual(["R1", "R2", "L1", "L2", "R4", "R3", "L3", "L4"], motion["servo_order"])
        for frame in motion["frames"]:
            self.assertEqual(8, len(frame["angles"]))
            self.assertTrue(all(0 <= angle <= 180 for angle in frame["angles"]))

    def test_motion_starts_and_ends_in_existing_firmware_stand_pose(self):
        motion = generate_motion("缓慢游泳")
        stand = [135, 45, 45, 135, 0, 180, 0, 180]
        self.assertEqual(stand, motion["frames"][0]["angles"])
        self.assertEqual(stand, motion["frames"][-1]["angles"])

    def test_frame_to_frame_step_is_bounded(self):
        motion = generate_motion("快速游泳", frame_ms=50)
        peak_step = max(
            abs(current - previous)
            for left, right in zip(motion["frames"], motion["frames"][1:])
            for previous, current in zip(left["angles"], right["angles"])
        )
        self.assertLessEqual(peak_step, 13)

    def test_diagonal_legs_share_phase_and_opposite_pair_differs(self):
        motion = generate_motion("游泳")
        paddle = next(frame for frame in motion["frames"] if frame["stage"] == "paddle")
        r1, r2, l1, l2 = paddle["angles"][:4]
        self.assertEqual(r1 - 90, l2 - 90)
        self.assertEqual(r2 - 90, l1 - 90)
        self.assertNotEqual(r1, r2)

    def test_writes_standalone_animation_preview(self):
        motion = generate_motion("游泳")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "swim.preview.html"
            write_preview(motion, output)
            html = output.read_text(encoding="utf-8")
        self.assertIn("const motion=", html)
        self.assertIn("generated_swim", html)
        self.assertIn("<canvas", html)


if __name__ == "__main__":
    unittest.main()
