from __future__ import annotations

import argparse
import json
from pathlib import Path

from .model import generate_motion
from .preview import write_preview


def main() -> int:
    parser = argparse.ArgumentParser(description="把自然语言动作转换为 Sesame V3 八舵机关键帧")
    parser.add_argument("prompt", help="动作描述，例如：快速游泳 4 次")
    parser.add_argument("--json", type=Path, default=Path("output/motion/generated.motion.json"))
    parser.add_argument("--preview", type=Path, default=Path("output/motion/generated.preview.html"))
    parser.add_argument("--frame-ms", type=int, default=50)
    args = parser.parse_args()

    motion = generate_motion(args.prompt, args.frame_ms)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(motion, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    write_preview(motion, args.preview)
    peak_step = max(
        abs(current - previous)
        for left, right in zip(motion["frames"], motion["frames"][1:])
        for previous, current in zip(left["angles"], right["angles"])
    )
    print(f"intent={motion['intent']['primitive']} style={motion['intent']['style']}")
    print(f"frames={len(motion['frames'])} duration_ms={motion['frames'][-1]['t_ms']} peak_step_deg={peak_step}")
    print(f"json={args.json.resolve()}")
    print(f"preview={args.preview.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
