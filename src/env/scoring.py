from dataclasses import dataclass
from typing import Any, Mapping, Optional

from mahjong.hand_calculating.hand import HandCalculator
from mahjong.hand_calculating.hand_config import HandConfig, OptionalRules
from mahjong.tile import TilesConverter

from .state import AfterKanPhase, GameState


def _safe_cost_float(value: object) -> float:
    if value is None or value == "":
        return 0.0
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


@dataclass(frozen=True)
class HandValue:
    """Stable score result independent of third-party result object identity."""

    error: Optional[str]
    cost: Mapping[str, float]
    han: int = 0
    fu: int = 0
    yaku: tuple[str, ...] = ()

    @property
    def is_valid(self) -> bool:
        return self.error is None

    @property
    def ron_points(self) -> float:
        return float(self.cost.get("main", 0.0))

    @property
    def tsumo_points(self) -> float:
        return float(self.cost.get("main", 0.0)) + 2.0 * float(
            self.cost.get("additional", 0.0)
        )


class ScoringService:
    """Scoring adapter for one hand; no match ledger is maintained."""

    def __init__(self, calculator: Optional[HandCalculator] = None) -> None:
        self.calculator = calculator or HandCalculator()

    @property
    def no_yaku_error(self) -> str:
        return str(getattr(self.calculator, "ERR_NO_YAKU", "no_yaku"))

    def estimate_tsumo(self, state: GameState, seat: int) -> HandValue:
        player = state.players[seat]
        last_drawn = player.state.last_drawn
        if last_drawn is None:
            return HandValue(error="no last drawn tile", cost={})
        return self._estimate(
            state=state,
            hand=player.hand,
            win_tile34=last_drawn,
            is_tsumo=True,
            is_riichi=player.state.riichi,
            player_wind=player.player_wind,
            is_rinshan=isinstance(state.phase, AfterKanPhase),
        )

    def estimate_ron(
        self,
        state: GameState,
        claimant: int,
        discarder: int,
        tile34: int,
    ) -> HandValue:
        player = state.players[claimant]
        hand = player.hand.copy()
        hand.add(tile34)
        return self._estimate(
            state=state,
            hand=hand,
            win_tile34=tile34,
            is_tsumo=False,
            is_riichi=player.state.riichi,
            player_wind=player.player_wind,
            is_rinshan=False,
        )

    def _estimate(
        self,
        *,
        state: GameState,
        hand,
        win_tile34: int,
        is_tsumo: bool,
        is_riichi: bool,
        player_wind: int,
        is_rinshan: bool,
    ) -> HandValue:
        win_counts = [0] * 34
        win_counts[win_tile34] = 1
        win_tile_136 = TilesConverter.to_136_array(win_counts)[0]

        tiles34 = hand.as_tile34()
        for meld in hand.melds:
            for tile34 in getattr(meld, "tiles_34", ()):
                tiles34[tile34] += 1
        # ``tiles_34`` exists in supported MahjongRepository versions.  The
        # fallback makes this adapter tolerant of versions exposing only tiles.
        if sum(tiles34) < hand.total_tiles() + sum(len(m.tiles) for m in hand.melds):
            tiles34 = hand.as_tile34()
            for meld in hand.melds:
                for tile136 in meld.tiles:
                    tiles34[tile136 // 4] += 1

        try:
            raw = self.calculator.estimate_hand_value(
                TilesConverter.to_136_array(tiles34),
                win_tile_136,
                melds=hand.melds,
                dora_indicators=state.wall.dora_indicators_136(
                    kan_count=state.kan_count,
                    end=True,
                    riichi=is_riichi,
                ),
                config=HandConfig(
                    is_tsumo=is_tsumo,
                    is_riichi=is_riichi,
                    is_rinshan=is_rinshan,
                    player_wind=player_wind,
                    # Preserve original effective behavior: closed hands do
                    # not need this option; opened hands enable open tanyao.
                    options=OptionalRules(has_open_tanyao=hand.is_open),
                ),
            )
        except Exception as exc:  # Convert library failures to stable results.
            return HandValue(error=f"scoring exception: {exc}", cost={})

        error = getattr(raw, "error", None)
        raw_cost: Any = getattr(raw, "cost", None) or {}
        cost = {
            str(key): _safe_cost_float(value) for key, value in dict(raw_cost).items()
        }
        raw_yaku = getattr(raw, "yaku", None) or ()
        return HandValue(
            error=str(error) if error else None,
            cost=cost,
            han=int(getattr(raw, "han", 0) or 0),
            fu=int(getattr(raw, "fu", 0) or 0),
            yaku=tuple(str(yaku) for yaku in raw_yaku),
        )
