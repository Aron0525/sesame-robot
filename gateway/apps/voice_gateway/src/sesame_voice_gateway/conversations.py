from __future__ import annotations

import time
import uuid
from collections.abc import Callable
from dataclasses import dataclass


class ConversationExpired(ValueError):
    """Raised when a requested conversation is no longer active."""


class ConversationOwnershipError(ValueError):
    """Raised when a device attempts to resume another owner's conversation."""


@dataclass(frozen=True, slots=True)
class Conversation:
    conversation_id: str
    device_id: str
    user_id: str
    expires_at: float


class ConversationRegistry:
    def __init__(
        self,
        *,
        ttl_seconds: int,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._ttl_seconds = ttl_seconds
        self._clock = clock
        self._conversations: dict[str, Conversation] = {}

    def resolve(
        self,
        *,
        requested_conversation_id: str | None,
        device_id: str,
        user_id: str,
    ) -> Conversation:
        now = self._clock()
        self._prune_expired_at(now)
        if requested_conversation_id is None:
            conversation = Conversation(
                conversation_id=f"conv_{uuid.uuid4().hex}",
                device_id=device_id,
                user_id=user_id,
                expires_at=now + self._ttl_seconds,
            )
            self._conversations[conversation.conversation_id] = conversation
            return conversation

        existing_conversation = self._conversations.get(requested_conversation_id)
        if existing_conversation is None or existing_conversation.expires_at <= now:
            self._conversations.pop(requested_conversation_id, None)
            raise ConversationExpired("conversation is expired or unknown")
        if (
            existing_conversation.device_id != device_id
            or existing_conversation.user_id != user_id
        ):
            raise ConversationOwnershipError("conversation does not belong to this device user")

        resumed = Conversation(
            conversation_id=existing_conversation.conversation_id,
            device_id=existing_conversation.device_id,
            user_id=existing_conversation.user_id,
            expires_at=now + self._ttl_seconds,
        )
        self._conversations[resumed.conversation_id] = resumed
        return resumed

    def resolve_or_reissue_for_authenticated_device(
        self,
        *,
        requested_conversation_id: str | None,
        device_id: str,
        user_id: str,
    ) -> Conversation:
        """Resume a known conversation or issue a replacement after expiry.

        This method is deliberately intended for the post-authentication path.
        An expired ID is normal after a gateway restart or TTL expiry, but an ID
        belonging to another authenticated device/user remains a hard failure.
        """

        try:
            return self.resolve(
                requested_conversation_id=requested_conversation_id,
                device_id=device_id,
                user_id=user_id,
            )
        except ConversationExpired:
            return self.resolve(
                requested_conversation_id=None,
                device_id=device_id,
                user_id=user_id,
            )

    def prune_expired(self) -> tuple[Conversation, ...]:
        return self._prune_expired_at(self._clock())

    def _prune_expired_at(self, now: float) -> tuple[Conversation, ...]:
        expired = tuple(
            conversation
            for conversation in self._conversations.values()
            if conversation.expires_at <= now
        )
        for conversation in expired:
            del self._conversations[conversation.conversation_id]
        return expired
