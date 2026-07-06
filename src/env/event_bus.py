from collections import defaultdict
from dataclasses import dataclass
from typing import Any, Callable, DefaultDict, Iterable, Mapping


@dataclass(frozen=True)
class Event:
    """A compatibility event message."""

    name: str
    payload: Mapping[str, Any]


Handler = Callable[[Event], None]


class EventBus:
    """Synchronous pub-sub adapter retained for external integrations."""

    def __init__(self) -> None:
        self._handlers: DefaultDict[str, list[Handler]] = defaultdict(list)

    def subscribe(self, name: str, handler: Handler) -> None:
        self._handlers[name].append(handler)

    def unsubscribe(self, name: str, handler: Handler) -> None:
        handlers = self._handlers.get(name)
        if not handlers:
            return
        try:
            handlers.remove(handler)
        except ValueError:
            return

    def publish(self, name: str, **payload: Any) -> None:
        event = Event(name=name, payload=dict(payload))
        for handler in tuple(self._handlers.get(name, ())):
            handler(event)

    def publish_many(self, events: Iterable[object]) -> None:
        """Forward domain events after a transition has already committed."""
        for event in events:
            name = getattr(event, "name")
            payload = getattr(event, "payload")
            self.publish(name, **payload)
