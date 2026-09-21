from dataclasses import dataclass
from typing import Any, Iterable, Optional

from mahjong.shanten import Shanten
from mahjong.constants import DRAGONS, EAST

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
    seat_rewards: tuple[float, ...]


class RewardProjector:
    """Computes rewards after a transition instead of inside game rules."""

    def __init__(
        self,
        config: Any,
        shanten_calc: Shanten,
        seats: int = 4,
        dealer_seat: int = 0,
        round_wind: int = EAST,
    ) -> None:
        self._config = config
        self._shanten = shanten_calc
        self._dealer_seat = dealer_seat
        self._round_wind = round_wind
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
        state: Optional[GameState] = None,
    ) -> RewardResult:
        seat_rewards = [0.0 for _ in self._memory]
        reward_update = self._project_discard(discard_snapshot)

        for event in events:
            if isinstance(event, RiichiDeclared) and event.seat == actor:
                seat_rewards[event.seat] += float(self._get("reward_riichi", 0.0))
            elif isinstance(event, MeldMade) and event.seat == actor:
                seat_rewards[event.seat] += self._confirmed_meld_reward(event, state)
            elif isinstance(event, WinRon) and event.seat == actor:
                points = event.score.ron_points * self._get("score_weight", 1.0)
                seat_rewards[event.seat] += points
                seat_rewards[event.from_seat] -= points
            elif isinstance(event, WinTsumo) and event.seat == actor:
                weight = self._get("score_weight", 1.0)
                main = float(event.score.cost.get("main", 0.0)) * weight
                additional = (
                    float(event.score.cost.get("additional", 0.0)) * weight
                )
                for seat in range(len(seat_rewards)):
                    if seat == event.seat:
                        continue
                    payment = (
                        main
                        if event.seat == self._dealer_seat
                        or seat == self._dealer_seat
                        else additional
                    )
                    seat_rewards[seat] -= payment
                    seat_rewards[event.seat] += payment

        return RewardResult(
            reward=float(seat_rewards[actor]),
            reward_update=float(reward_update),
            seat_rewards=tuple(float(value) for value in seat_rewards),
        )

    def _confirmed_meld_reward(
        self,
        event: MeldMade,
        state: Optional[GameState],
    ) -> float:
        if state is None or event.kind not in {"pon", "minkan", "ankan"}:
            return 0.0
        if not event.tiles34 or len(set(event.tiles34)) != 1:
            return 0.0

        tile34 = event.tiles34[0]
        player_wind = state.players[event.seat].player_wind
        confirmed_han = int(tile34 in DRAGONS)
        confirmed_han += int(tile34 == player_wind)
        confirmed_han += int(tile34 == self._round_wind)

        reward = confirmed_han * self._get("reward_confirmed_yaku_han", 5.0)
        return min(reward, self._get("reward_meld_cap", 20.0))

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
            delta = snapshot.available - memory.last_available
            if delta >= 0:
                update += delta
            else:
                update += delta * float(self._get("penalty_ava_num", 1.0))

        memory.last_shanten = snapshot.shanten
        memory.last_available = snapshot.available
        return float(update)

    def _get(self, name: str, default: float) -> float:
        return float(getattr(self._config, name, default))
