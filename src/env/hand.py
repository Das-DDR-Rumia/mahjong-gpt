from typing import Optional, Sequence, Tuple

from mahjong.meld import Meld
from mahjong.shanten import Shanten
from mahjong.tile import TilesConverter

from .event_bus import EventBus
from .tiles import (
    ChiOption,
    chi_option_from_sequence,
    iter_chi_sequences_containing,
    is_suit,
)


class MahjongHand:
    """Concealed tile34 counts plus meld records."""

    def __init__(
        self, seat: Optional[int] = None, bus: Optional[EventBus] = None
    ) -> None:
        self._counts: list[int] = [0] * 34
        self.melds: list[Meld] = []
        self._seat = seat
        self._bus = bus

    def copy(self) -> "MahjongHand":
        copied = MahjongHand()
        copied._counts = self._counts.copy()
        copied.melds = self.melds.copy()
        return copied

    def as_tile34(self) -> list[int]:
        return self._counts.copy()

    def total_tiles(self) -> int:
        return sum(self._counts)

    def count(self, tile34: int) -> int:
        return self._counts[tile34]

    @property
    def is_open(self) -> bool:
        return any(bool(getattr(meld, "opened", False)) for meld in self.melds)

    def add(self, tile34: int, n: int = 1) -> None:
        if not 0 <= tile34 < 34:
            raise ValueError(f"tile34 out of range: {tile34}")
        if n < 0:
            raise ValueError("n must be non-negative")
        if self._counts[tile34] + n > 4:
            raise ValueError("tile count would exceed 4")
        self._counts[tile34] += n

    def remove(self, tile34: int, n: int = 1) -> None:
        if not 0 <= tile34 < 34:
            raise ValueError(f"tile34 out of range: {tile34}")
        if n < 0:
            raise ValueError("n must be non-negative")
        if self._counts[tile34] < n:
            raise ValueError("tile count would go negative")
        self._counts[tile34] -= n

    def shanten(self, shanten_calc: Shanten) -> int:
        return shanten_calc.calculate_shanten(self.as_tile34())

    def possible_chi(self, discard_tile34: int) -> list[ChiOption]:
        if not is_suit(discard_tile34):
            return []
        options: list[ChiOption] = []
        for sequence in iter_chi_sequences_containing(discard_tile34):
            option = chi_option_from_sequence(discard_tile34, sequence)
            first, second = option.need_remove
            if self._counts[first] > 0 and self._counts[second] > 0:
                options.append(option)
        return options

    def available_improvement_count(
        self,
        shanten_calc: Shanten,
        remaining_tile34: Sequence[int],
    ) -> int:
        """Keep the original reward-shaping ukeire calculation semantics."""
        tiles = self.as_tile34()
        now_shanten = shanten_calc.calculate_shanten(tiles)
        unique_remaining = set(remaining_tile34)
        improving_draws: set[int] = set()

        for discard, count in enumerate(tiles):
            if count <= 0:
                continue
            after_discard = tiles.copy()
            after_discard[discard] -= 1
            for draw in unique_remaining:
                after_draw = after_discard.copy()
                after_draw[draw] += 1
                if shanten_calc.calculate_shanten(after_draw) < now_shanten:
                    improving_draws.add(draw)

        return sum(remaining_tile34.count(tile) for tile in improving_draws)

    def add_open_meld_chi(self, sequence: Tuple[int, int, int]) -> None:
        self.melds.append(self._meld(Meld.CHI, sequence, opened=True))
        self._publish_meld("chi", list(sequence))

    def add_open_meld_pon(self, tile34: int) -> None:
        self.melds.append(self._meld(Meld.PON, (tile34, tile34, tile34), opened=True))
        self._publish_meld("pon", [tile34] * 3)

    def add_open_meld_kan(self, tile34: int) -> None:
        self.melds.append(
            self._meld(Meld.KAN, (tile34, tile34, tile34, tile34), opened=True)
        )
        self._publish_meld("minkan", [tile34] * 4)

    def add_closed_kan(self, tile34: int) -> None:
        self.melds.append(
            self._meld(Meld.KAN, (tile34, tile34, tile34, tile34), opened=False)
        )
        self._publish_meld("ankan", [tile34] * 4)

    def has_open_pon(self, tile34: int) -> bool:
        for meld in self.melds:
            if getattr(meld, "meld_type", None) != Meld.PON or not getattr(
                meld, "opened", False
            ):
                continue
            if all(tile // 4 == tile34 for tile in meld.tiles):
                return True
        return False

    def upgrade_pon_to_kan(self, tile34: int) -> None:
        for index, meld in enumerate(self.melds):
            if (
                getattr(meld, "meld_type", None) == Meld.PON
                and bool(getattr(meld, "opened", False))
                and all(tile // 4 == tile34 for tile in meld.tiles)
            ):
                del self.melds[index]
                self.melds.append(
                    self._meld(Meld.KAN, (tile34, tile34, tile34, tile34), opened=True)
                )
                if self._bus is not None and self._seat is not None:
                    self._bus.publish(
                        "meld_upgraded", seat=self._seat, kind="kakan", tile34=tile34
                    )
                return
        raise RuntimeError("no matching opened pon to upgrade")

    @staticmethod
    def _meld(meld_type: int, tiles34: Tuple[int, ...], opened: bool) -> Meld:
        counts = [0] * 34
        for tile in tiles34:
            counts[tile] += 1
        return Meld(
            meld_type=meld_type,
            tiles=TilesConverter.to_136_array(counts),
            opened=opened,
        )

    def _publish_meld(self, kind: str, tiles34: list[int]) -> None:
        if self._bus is not None and self._seat is not None:
            self._bus.publish("meld_made", seat=self._seat, kind=kind, tiles34=tiles34)
