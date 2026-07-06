from dataclasses import dataclass
from typing import Optional

from .event_bus import EventBus
from .hand import MahjongHand


@dataclass
class PlayerState:
    riichi: bool = False
    riichi_lock: bool = False
    last_drawn: Optional[int] = None


class MahjongPlayer:
    """A player entity.  Turn flow is intentionally owned by the engine."""

    def __init__(
        self,
        seat: int,
        bus: Optional[EventBus] = None,
        player_wind: int = 0,
    ) -> None:
        self.seat = seat
        self.bus = bus
        self.player_wind = player_wind
        self.hand = MahjongHand(seat=seat, bus=bus)
        self.discards: list[int] = []
        self.state = PlayerState()

    def reset(self) -> None:
        self.hand = MahjongHand(seat=self.seat, bus=self.bus)
        self.discards = []
        self.state = PlayerState()

    def draw(self, tile34: int) -> None:
        self.hand.add(tile34)
        self.state.last_drawn = tile34
        if self.bus is not None:
            self.bus.publish("tile_drawn", seat=self.seat, tile34=tile34)

    def discard(self, tile34: int) -> None:
        self.hand.remove(tile34)
        self.discards.append(tile34)
        if self.bus is not None:
            self.bus.publish("tile_discarded", seat=self.seat, tile34=tile34)

    def pop_last_discard_for_call(self, expected_tile34: int) -> None:
        if not self.discards:
            raise RuntimeError("no discard to call")
        actual = self.discards[-1]
        if actual != expected_tile34:
            raise RuntimeError(
                f"discard mismatch: last={actual}, expected={expected_tile34}"
            )
        self.discards.pop()

    @property
    def melds(self):
        return self.hand.melds
