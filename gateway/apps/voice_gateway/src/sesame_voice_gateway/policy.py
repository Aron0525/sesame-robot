from __future__ import annotations

from sesame_voice_gateway.providers.base import AgentResult, AsrResult

ALLOWED_ACTIONS = frozenset({"stop", "wave", "rest", "stand"})
ALLOWED_EXPRESSIONS = frozenset(
    {
        "idle",
        "happy",
        "sad",
        "angry",
        "surprised",
        "sleepy",
        "love",
        "excited",
        "confused",
        "thinking",
    }
)
ALLOWED_VOICE_IDS = frozenset({"sesame_default"})
ALLOWED_VOICE_STYLES = frozenset(
    {"neutral", "happy", "sad", "angry", "surprised", "thinking"}
)


class PolicyViolation(ValueError):
    """Raised when Agent output is unsafe or outside device capabilities."""


def validate_asr_result(result: AsrResult) -> AsrResult:
    if not result.text.strip():
        raise PolicyViolation("ASR text must not be empty")
    if len(result.text) > 8_000:
        raise PolicyViolation("ASR text exceeds maximum length")
    if result.confidence is not None and not 0 <= result.confidence <= 1:
        raise PolicyViolation("ASR confidence is outside allowed range")
    return result


def validate_agent_result(result: AgentResult) -> AgentResult:
    if not result.text.strip():
        raise PolicyViolation("agent reply text must not be empty")
    if len(result.text) > 16_000:
        raise PolicyViolation("agent reply text exceeds maximum length")
    if result.expression.name not in ALLOWED_EXPRESSIONS:
        raise PolicyViolation(f"expression is not allowed: {result.expression.name}")
    if not 100 <= result.expression.ttl_ms <= 10_000:
        raise PolicyViolation("expression ttl_ms is outside allowed range")
    if not 0.5 <= result.voice.speed <= 2.0:
        raise PolicyViolation("voice speed is outside allowed range")
    if result.voice.voice_id not in ALLOWED_VOICE_IDS:
        raise PolicyViolation(f"voice_id is not allowed: {result.voice.voice_id}")
    if result.voice.style not in ALLOWED_VOICE_STYLES:
        raise PolicyViolation(f"voice style is not allowed: {result.voice.style}")
    if len(result.actions) > 1:
        raise PolicyViolation("response.plan permits at most one action")

    for action in result.actions:
        if action.name not in ALLOWED_ACTIONS:
            raise PolicyViolation(f"action is not allowed: {action.name}")
        if not 100 <= action.duration_ms <= 5_000:
            raise PolicyViolation(f"action duration is unsafe: {action.name}")

    return result
