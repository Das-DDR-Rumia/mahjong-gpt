from collections import deque
from typing import Deque
import random

from mahjong.tile import TilesConverter


class MahjongWall:
    """A 136-tile wall represented internally as tile34 values.

    The initial ten dead-wall tiles are retained as dora/ura indicators and
    four tiles are the initial rinshan supply, matching the original project.
    On a kan, one live-wall tail tile is moved into the dead-wall replacement
    area.  This fixes the original wall-length bug without adding new game
    rules.
    """

    def __init__(self, rng: random.Random) -> None:
        self._rng = rng
        self.live: Deque[int] = deque()
        self.dora_indicators: list[int] = []
        self.rinshan: Deque[int] = deque()
        self.dead_wall_replacements: list[int] = []

    def reset(self) -> None:
        tiles = [tile for tile in range(34) for _ in range(4)]
        self._rng.shuffle(tiles)
        self.dora_indicators = tiles[:10]
        self.rinshan = deque(tiles[10:14])
        self.live = deque(tiles[14:])
        self.dead_wall_replacements = []

    def remaining_live(self) -> int:
        return len(self.live)

    def draw(self) -> int:
        if not self.live:
            raise RuntimeError("live wall is empty")
        return self.live.popleft()

    def draw_rinshan(self) -> int:
        """Draw a replacement tile and shorten the live wall by one tile."""
        if not self.rinshan:
            raise RuntimeError("rinshan is empty")
        if not self.live:
            raise RuntimeError("cannot draw rinshan after live wall exhaustion")
        tile = self.rinshan.popleft()
        self.dead_wall_replacements.append(self.live.pop())
        return tile

    def dora_indicators_136(
        self,
        kan_count: int,
        end: bool = True,
        riichi: bool = False,
    ) -> list[int]:
        """Return visible dora and, for a riichi winning hand, ura indicators."""
        if not 0 <= kan_count <= 4:
            raise ValueError(f"invalid kan count: {kan_count}")
        shown = kan_count + 1
        indicator_tiles = self.dora_indicators[:shown]
        if end and riichi:
            indicator_tiles = indicator_tiles + self.dora_indicators[5 : 5 + shown]
        counts = [0] * 34
        for tile in indicator_tiles:
            counts[tile] += 1
        return TilesConverter.to_136_array(counts)

    def remaining_tile34_list(self) -> list[int]:
        """Live-wall tiles used by existing reward-shaping semantics."""
        return list(self.live)

    def all_unseen_tile34_list(self) -> list[int]:
        """All not-yet-held tiles, including dead-wall inventory."""
        return (
            list(self.live)
            + list(self.rinshan)
            + self.dora_indicators.copy()
            + self.dead_wall_replacements.copy()
        )
