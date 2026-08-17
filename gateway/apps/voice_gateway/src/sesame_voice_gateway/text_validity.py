"""Classify completed ASR text before it can invoke an assistant response."""

from __future__ import annotations

import re
from enum import StrEnum


class TranscriptDisposition(StrEnum):
    BLANK = "blank"
    FILLER_ONLY = "filler_only"
    VALID = "valid"


# Do not use a text-length threshold here: short requests such as “暂停” and
# “再见” carry clear intent. This pattern is deliberately narrow so phrases
# such as “那个地方” and “就是暂停” remain valid turns.
_FILLER_PATTERN = re.compile(r"(?:嗯|啊|呃|额|哦|哈|那个|就是)+")


def _remove_separators(text: str) -> str:
    # Python's stdlib `re` has no Unicode \p character classes. Keep CJK
    # punctuation explicitly and regard every non-word character as a split.
    return re.sub(r"[\s\W_]+", "", text, flags=re.UNICODE)


def classify_transcript(text: str) -> TranscriptDisposition:
    """Return whether completed ASR text should be discarded or processed."""
    compact = _remove_separators(text)
    if not compact:
        return TranscriptDisposition.BLANK
    if _FILLER_PATTERN.fullmatch(compact):
        return TranscriptDisposition.FILLER_ONLY
    return TranscriptDisposition.VALID
