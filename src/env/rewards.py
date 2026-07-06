from dataclasses import dataclass
from typing import Any, Iterable, Optional

from mahjong.shanten import Shanten

from .constant import DISCARD_MAX, DISCARD_MIN
from .events import MeldMade, RiichiDeclared, WinRon, WinTsumo
from .state import GameState


@dataclass
class RewardMemory:
    first_round: bool = True
    last_shanten: Optional[int] = None
    last_available: Optional[int] = None


@dataclass(frozen=True)
class DiscardSnapshot:
    seat: int
    shanten: int
    available: int


@dataclass(frozen=True)
class RewardResult:
    reward: float
    reward_update: float


class RewardProjector:
    """Computes rewards after a transition instead of inside game rules."""

    def __init__(self, config: Any, shanten_calc: Shanten, seats: int = 4) -> None:
        self._config = config
        self._shanten = shanten_calc
        self._memory = [RewardMemory() for _ in range(seats)]

    def reset(self) -> None:
        self._memory = [RewardMemory() for _ in self._memory]

    def capture_before_action(
        self,
        state: GameState,
        actor: int,
        action: int,
    ) -> Optional[DiscardSnapshot]:
        if not DISCARD_MIN <= action <= DISCARD_MAX:
            return None
        player = state.players[actor]
        return DiscardSnapshot(
            seat=actor,
            shanten=player.hand.shanten(self._shanten),
            available=player.hand.available_improvement_count(
                self._shanten,
                state.wall.remaining_tile34_list(),
            ),
        )

    def project(
        self,
        *,
        actor: int,
        events: Iterable[object],
        discard_snapshot: Optional[DiscardSnapshot],
    ) -> RewardResult:
        immediate = 0.0
        reward_update = self._project_discard(discard_snapshot)

        for event in events:
            if isinstance(event, RiichiDeclared) and event.seat == actor:
                immediate += float(self._get("reward_riichi", 0.0))
            elif (
                isinstance(event, MeldMade)
                and event.seat == actor
                and event.kind
                in {
                    "chi",
                    "pon",
                    "minkan",
                }
            ):
                immediate += float(self._get("reward_open_tanyao", 0.0))
            elif isinstance(event, WinRon) and event.seat == actor:
                immediate += event.score.ron_points * float(
                    self._get("score_weight", 1.0)
                )
            elif isinstance(event, WinTsumo) and event.seat == actor:
                immediate += event.score.tsumo_points * float(
                    self._get("score_weight", 1.0)
                )

        return RewardResult(reward=float(immediate), reward_update=float(reward_update))

    def _project_discard(self, snapshot: Optional[DiscardSnapshot]) -> float:
        if snapshot is None:
            return 0.0
        memory = self._memory[snapshot.seat]
        if memory.first_round:
            memory.first_round = False
            memory.last_shanten = snapshot.shanten
            memory.last_available = snapshot.available
            return 0.0

        update = 0.0
        if memory.last_shanten is not None and snapshot.shanten != memory.last_shanten:
            delta = memory.last_shanten - snapshot.shanten
            update += delta if snapshot.shanten <= memory.last_shanten else delta * 2
            update *= float(self._get("reward_weight_shanten", 1.0))

        # This intentionally reproduces the original shaping arithmetic rather
        # than silently changing an existing training target.
        if (
            memory.last_available is not None
            and snapshot.available != memory.last_available
            and memory.last_shanten == snapshot.shanten
        ):
            delta = memory.last_available - snapshot.available
            if snapshot.available <= memory.last_available:
                update += delta
            else:
                update += delta * float(self._get("penalty_ava_num", 1.0))

        memory.last_shanten = snapshot.shanten
        memory.last_available = snapshot.available
        return float(update)

    def _get(self, name: str, default: float) -> float:
        return float(getattr(self._config, name, default))
