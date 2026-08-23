from __future__ import annotations

import json
import math
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, List, Sequence


MODEL_PATH = Path(__file__).with_name("robot_model.json")


@dataclass(frozen=True)
class MotionIntent:
    primitive: str
    style: str
    cycles: int
    cycle_ms: int
    amplitude: float
    confidence: float
    rationale: str


def load_robot_model(path: Path = MODEL_PATH) -> Dict[str, object]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_motion_intent(prompt: str) -> MotionIntent:
    """Turn a small natural-language vocabulary into a constrained motion intent.

    This is deliberately deterministic: an LLM can replace this parser later, while
    the geometry, limits and validation remain outside the LLM.
    """
    text = prompt.strip().lower()
    if not text:
        raise ValueError("动作描述不能为空")
    if not any(token in text for token in ("游泳", "划水", "狗刨", "swim", "paddle")):
        raise ValueError("当前原型只实现了游泳/划水动作基元")

    style = "normal"
    cycle_ms = 1200
    amplitude = 1.0
    if any(token in text for token in ("快", "快速", "激烈", "fast")):
        style, cycle_ms, amplitude = "energetic", 850, 1.15
    elif any(token in text for token in ("慢", "缓慢", "轻柔", "slow", "gentle")):
        style, cycle_ms, amplitude = "gentle", 1600, 0.72

    cycles = 4
    digit_match = re.search(r"([1-8])\s*(?:次|圈|个周期|cycles?)", text)
    if digit_match:
        cycles = int(digit_match.group(1))

    return MotionIntent(
        primitive="dog_paddle",
        style=style,
        cycles=cycles,
        cycle_ms=cycle_ms,
        amplitude=amplitude,
        confidence=0.94,
        rationale="游泳被分解为四腿交替划水；两组对角腿相差 180°，髋关节摆动、膝关节在回程时收腿。",
    )


def _smoothstep(value: float) -> float:
    return value * value * (3.0 - 2.0 * value)


def _interpolate(start: Sequence[float], end: Sequence[float], ratio: float) -> List[float]:
    eased = _smoothstep(max(0.0, min(1.0, ratio)))
    return [a + (b - a) * eased for a, b in zip(start, end)]


def _paddle_pose(phase: float, amplitude: float) -> List[float]:
    # Raw servo coordinates. Direction signs make mirrored joints move in the
    # same semantic direction. Diagonal legs share a phase, the other pair is π apart.
    directions = {"R1": 1, "R2": -1, "L1": -1, "L2": 1,
                  "R4": -1, "R3": 1, "L3": -1, "L4": 1}
    leg_phase = {
        "R1": phase,
        "R2": phase + math.pi,
        "L1": phase + math.pi,
        "L2": phase,
    }
    knee_for_hip = {"R1": "R3", "R2": "R4", "L1": "L3", "L2": "L4"}
    values: Dict[str, float] = {}
    for hip, local_phase in leg_phase.items():
        # Hip produces the forward/back stroke. Knee bends primarily on recovery.
        stroke = math.sin(local_phase)
        recovery = 0.5 * (1.0 + math.cos(local_phase))
        values[hip] = 90.0 + directions[hip] * (12.0 + 30.0 * amplitude * stroke)
        knee = knee_for_hip[hip]
        values[knee] = 90.0 + directions[knee] * (18.0 + 34.0 * amplitude * recovery)

    order = ["R1", "R2", "L1", "L2", "R4", "R3", "L3", "L4"]
    return [values[name] for name in order]


def _clamp_and_round(angles: Sequence[float], robot: Dict[str, object]) -> List[int]:
    order = robot["servo_order"]
    servos = robot["servos"]
    result = []
    for name, angle in zip(order, angles):
        limits = servos[name]
        result.append(round(max(limits["min_deg"], min(limits["max_deg"], angle))))
    return result


def generate_motion(prompt: str, frame_ms: int = 50) -> Dict[str, object]:
    if frame_ms < 20 or frame_ms > 200:
        raise ValueError("frame_ms 必须在 20..200 之间")

    robot = load_robot_model()
    intent = parse_motion_intent(prompt)
    stand = [float(value) for value in robot["stand_angles"]]
    start_phase = -math.pi / 2.0
    first_paddle = _paddle_pose(start_phase, intent.amplitude)
    frames: List[Dict[str, object]] = []

    def append_frame(time_ms: int, angles: Sequence[float], stage: str) -> None:
        frames.append({
            "t_ms": time_ms,
            "angles": _clamp_and_round(angles, robot),
            "stage": stage,
        })

    enter_ms = 900
    for t_ms in range(0, enter_ms, frame_ms):
        append_frame(t_ms, _interpolate(stand, first_paddle, t_ms / enter_ms), "enter")

    paddle_start = enter_ms
    paddle_duration = intent.cycles * intent.cycle_ms
    for elapsed in range(0, paddle_duration, frame_ms):
        phase = start_phase + 2.0 * math.pi * elapsed / intent.cycle_ms
        append_frame(paddle_start + elapsed, _paddle_pose(phase, intent.amplitude), "paddle")

    last_paddle = _paddle_pose(
        start_phase + 2.0 * math.pi * paddle_duration / intent.cycle_ms,
        intent.amplitude,
    )
    exit_start = paddle_start + paddle_duration
    exit_ms = 900
    for elapsed in range(0, exit_ms, frame_ms):
        append_frame(
            exit_start + elapsed,
            _interpolate(last_paddle, stand, elapsed / exit_ms),
            "exit",
        )
    append_frame(exit_start + exit_ms, stand, "stand")

    return {
        "schema": "sesame.motion.v1",
        "name": "generated_swim",
        "source_prompt": prompt,
        "model": robot["model"],
        "servo_order": robot["servo_order"],
        "frame_ms": frame_ms,
        "loop": False,
        "intent": asdict(intent),
        "safety": {
            "limits_source": robot["calibration_status"],
            "requires_low_speed_hardware_preview": True,
        },
        "frames": frames,
    }
