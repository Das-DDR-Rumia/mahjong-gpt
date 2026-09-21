from typing import Dict, Optional, Tuple
import random

from mahjong.hand_calculating.hand import HandCalculator
from mahjong.shanten import Shanten

try:  # Existing project layout: package_parent/config.py
    from ..config import RewardConfig  # type: ignore
except ImportError:  # Standalone or copied-package fallback.
    from .config import RewardConfig

from .claims import PendingClaim
from .constant import (
    ACTIONS_PER_SEAT,
    CHI_DOWN,
    CHI_MID,
    CHI_UP,
    DISCARD_MAX,
    DISCARD_MIN,
    KAN_ADD,
    KAN_CLOSED,
    KAN_OPEN,
    PASS,
    PLAYER_TOKENS,
    PON,
    RIICHI,
    RON,
    TSUMO,
    WIND_IDX_MAP,
)
from .encoding import ObservationEncoder
from .engine import MahjongEngine
from .event_bus import EventBus
from .invariants import assert_integrity as assert_state_integrity
from .legal import LegalDecision
from .player import MahjongPlayer
from .rewards import RewardProjector
from .scoring import ScoringService
from .state import (
    AfterCallDiscardPhase,
    AfterKanPhase,
    ClaimPhase,
    GameState,
    RiichiDiscardPhase,
)
from .tokens import TokenList
from .tiles import tile34_to_str
from .wall import MahjongWall


class MahjongEnv:
    """Four-seat single-hand Mahjong environment.

    Public methods intentionally preserve the original Gym-like contract:
    ``reset(seed) -> (obs, reward, done, info)`` and
    ``step(action) -> (obs, reward, done, info)``.
    """

    def __init__(
        self,
        seed: Optional[int] = None,
        rc: Optional[RewardConfig] = None,
    ) -> None:
        self.rng = random.Random(seed)
        self.bus = EventBus()
        self.rc = rc or RewardConfig()

        self.shanten_calc = Shanten()
        self.score_calc = HandCalculator()
        self.scoring = ScoringService(self.score_calc)
        self.engine = MahjongEngine(self.shanten_calc, self.scoring)
        self.rewards = RewardProjector(self.rc, self.shanten_calc)
        self.encoder = ObservationEncoder()

        self.wall = MahjongWall(self.rng)
        # ``bus=None`` prevents low-level mutation methods from publishing
        # before the full engine transition commits.  ``self.bus`` receives the
        # same event names after commit in ``reset``/``step``.
        self.players = [
            MahjongPlayer(seat, bus=None, player_wind=WIND_IDX_MAP[seat])
            for seat in range(4)
        ]
        self._state: Optional[GameState] = None
        self.history_tokens = TokenList.empty()

    def reset(self, seed: Optional[int] = None) -> Tuple[Dict, float, bool, Dict]:
        """Reset one hand and deal the initial 13/13/13/14 distribution."""
        if seed is not None:
            # Keep the wall's RNG reference valid instead of replacing it.
            self.rng.seed(seed)

        self.rewards.reset()
        self._state, events = self.engine.new_hand(self.wall, self.players)
        self.history_tokens = TokenList.from_ids([PLAYER_TOKENS[self.seat_now]])
        self.bus.publish_many(events)
        obs, info = self._build_obs_and_info(
            reward_update=0.0,
            seat_rewards=(0.0, 0.0, 0.0, 0.0),
        )
        return obs, 0.0, self.done, info

    def step(self, action: int) -> Tuple[Dict, float, bool, Dict]:
        """Apply exactly one legal decision of the currently active seat."""
        state = self._require_state()
        if self.done:
            obs, info = self._build_obs_and_info(
                reward_update=0.0,
                seat_rewards=(0.0, 0.0, 0.0, 0.0),
            )
            return obs, 0.0, True, info

        decision = self.engine.legal.analyze(state)
        actor = state.actor_seat
        snapshot = self.rewards.capture_before_action(state, actor, action)
        transition = self.engine.step(state, action, decision)

        # History only changes after success; returned observations own a copy.
        self.history_tokens.append(action)
        if not self.done:
            self.history_tokens.append(PLAYER_TOKENS[self.seat_now])

        self.bus.publish_many(transition.events)
        reward_result = self.rewards.project(
            actor=transition.actor,
            events=transition.events,
            discard_snapshot=snapshot,
            state=state,
        )
        obs, info = self._build_obs_and_info(
            reward_update=reward_result.reward_update,
            seat_rewards=reward_result.seat_rewards,
        )
        return obs, reward_result.reward, self.done, info

    # Compatibility projections for users who inspected the old environment.
    @property
    def done(self) -> bool:
        return self._require_state().done

    @property
    def seat_now(self) -> int:
        return self._require_state().actor_seat

    def _build_obs_and_info(
        self,
        reward_update: float,
        seat_rewards: tuple[float, ...],
    ) -> Tuple[Dict, Dict]:
        decision: LegalDecision = self.engine.legal.analyze(self._require_state())
        return self.encoder.build(
            self._require_state(),
            self.history_tokens,
            decision,
            reward_update,
            seat_rewards,
        )

    def assert_integrity(self) -> None:
        assert_state_integrity(self._require_state())

    def _require_state(self) -> GameState:
        if self._state is None:
            raise RuntimeError("reset() must be called before accessing state")
        return self._state


def action_to_str(action: int) -> str:
    if DISCARD_MIN <= action <= DISCARD_MAX:
        return f"discards {tile34_to_str(action)}"
    if action in (CHI_UP, CHI_MID, CHI_DOWN):
        return "calls CHI"
    if action == PON:
        return "calls PON"
    if action in (KAN_OPEN, KAN_ADD, KAN_CLOSED):
        return "calls KAN"
    if action == RIICHI:
        return "declares RIICHI"
    if action == RON:
        return "calls RON"
    if action == TSUMO:
        return "calls TSUMO"
    if action == PASS:
        return "PASS"
    return f"action {action}"
