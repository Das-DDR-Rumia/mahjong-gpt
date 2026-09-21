from typing import Dict, Tuple

from .legal import LegalDecision
from .state import GameState
from .tokens import TokenList


class ObservationEncoder:
    """Build snapshots without mutating state or token history."""

    def build(
        self,
        state: GameState,
        history: TokenList,
        decision: LegalDecision,
        reward_update: float,
        seat_rewards: tuple[float, ...],
    ) -> Tuple[Dict, Dict]:
        seat = state.actor_seat
        observation = {
            "tokens": history.copy(),
            "is_three": 0,
            "seat": seat,
            "hand": state.players[seat].hand.as_tile34(),
        }
        info = {
            "action_mask": list(decision.mask),
            "reward_update": float(reward_update),
            "seat_rewards": [float(value) for value in seat_rewards],
        }
        return observation, info
