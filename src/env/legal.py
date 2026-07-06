from dataclasses import dataclass, field
from typing import Optional

from mahjong.shanten import Shanten

from .claims import PendingClaim, current_offers
from .constant import (
    ACTIONS_PER_SEAT,
    KAN_ADD,
    KAN_CLOSED,
    PASS,
    RIICHI,
    TSUMO,
)
from .scoring import HandValue, ScoringService
from .state import (
    AfterCallDiscardPhase,
    AfterKanPhase,
    ClaimPhase,
    GameState,
    RiichiDiscardPhase,
    TerminalPhase,
    TurnPhase,
)


@dataclass(frozen=True)
class LegalDecision:
    """Mask plus deterministic payload choices for legacy action ids."""

    mask: tuple[int, ...]
    closed_kan_tile: Optional[int] = None
    added_kan_tile: Optional[int] = None
    tsumo_score: Optional[HandValue] = None
    claim_by_action: dict[int, PendingClaim] = field(default_factory=dict)

    def is_legal(self, action: int) -> bool:
        return 0 <= action < len(self.mask) and bool(self.mask[action])


class LegalActionAnalyzer:
    """Computes every public action mask used by both ``step`` and ``info``."""

    def __init__(self, shanten_calc: Shanten, scoring: ScoringService) -> None:
        self._shanten = shanten_calc
        self._scoring = scoring

    def analyze(self, state: GameState) -> LegalDecision:
        if isinstance(state.phase, TerminalPhase):
            return LegalDecision(mask=self._empty_mask())
        if isinstance(state.phase, ClaimPhase):
            return self._claim_decision(state)
        if isinstance(state.phase, RiichiDiscardPhase):
            mask = self._empty_mask()
            for tile34 in state.phase.legal_discards:
                mask[tile34] = 1
            return LegalDecision(mask=tuple(mask))
        if isinstance(state.phase, AfterCallDiscardPhase):
            mask = self._empty_mask()
            self._fill_discard_mask(mask, state)
            return LegalDecision(mask=tuple(mask))
        if isinstance(state.phase, AfterKanPhase):
            return self._after_kan_decision(state)
        if isinstance(state.phase, TurnPhase):
            return self._turn_decision(state)
        raise RuntimeError(f"unknown phase: {state.phase!r}")

    def riichi_discard_tiles(self, state: GameState, seat: int) -> tuple[int, ...]:
        hand = state.players[seat].hand
        legal: list[int] = []
        for tile34, count in enumerate(hand.as_tile34()):
            if count <= 0:
                continue
            hand.remove(tile34)
            try:
                if hand.shanten(self._shanten) == 0:
                    legal.append(tile34)
            finally:
                hand.add(tile34)
        return tuple(legal)

    def _turn_decision(self, state: GameState) -> LegalDecision:
        mask = self._empty_mask()
        seat = state.phase.seat
        player = state.players[seat]
        tsumo_score = self._tsumo_score_if_legal(state, seat)
        if tsumo_score is not None:
            mask[TSUMO] = 1

        if (
            not player.state.riichi
            and not player.hand.melds
            and player.hand.shanten(self._shanten) == 0
            and self.riichi_discard_tiles(state, seat)
        ):
            mask[RIICHI] = 1

        closed_kan_tile = None
        added_kan_tile = None

        if state.kan_count < 4 and state.wall.remaining_live() > 0:
            closed_candidates = [
                tile34
                for tile34, count in enumerate(player.hand.as_tile34())
                if count == 4
            ]
            if closed_candidates:
                closed_kan_tile = min(closed_candidates)
                mask[KAN_CLOSED] = 1

            added_candidates = [
                tile34
                for tile34, count in enumerate(player.hand.as_tile34())
                if count >= 1 and player.hand.has_open_pon(tile34)
            ]
            if added_candidates:
                added_kan_tile = min(added_candidates)
                mask[KAN_ADD] = 1

        self._fill_discard_mask(mask, state)
        return LegalDecision(
            mask=tuple(mask),
            closed_kan_tile=closed_kan_tile,
            added_kan_tile=added_kan_tile,
            tsumo_score=tsumo_score,
        )

    def _after_kan_decision(self, state: GameState) -> LegalDecision:
        mask = self._empty_mask()
        seat = state.phase.seat
        tsumo_score = self._tsumo_score_if_legal(state, seat)
        if tsumo_score is not None:
            mask[TSUMO] = 1
        self._fill_discard_mask(mask, state)
        return LegalDecision(mask=tuple(mask), tsumo_score=tsumo_score)

    def _claim_decision(self, state: GameState) -> LegalDecision:
        phase = state.phase
        assert isinstance(phase, ClaimPhase)
        mask = self._empty_mask()
        by_action: dict[int, PendingClaim] = {}
        for claim in current_offers(phase):
            mask[claim.action] = 1
            by_action[claim.action] = claim
        mask[PASS] = 1
        return LegalDecision(mask=tuple(mask), claim_by_action=by_action)

    def _tsumo_score_if_legal(self, state: GameState, seat: int) -> Optional[HandValue]:
        player = state.players[seat]
        if player.hand.shanten(self._shanten) != -1:
            return None
        score = self._scoring.estimate_tsumo(state, seat)
        return score if score.is_valid else None

    @staticmethod
    def _empty_mask() -> list[int]:
        return [0] * ACTIONS_PER_SEAT

    def _fill_discard_mask(self, mask: list[int], state: GameState) -> None:
        seat = state.actor_seat
        player = state.players[seat]
        if player.state.riichi and player.state.riichi_lock:
            if player.state.last_drawn is not None:
                mask[player.state.last_drawn] = 1
            return
        for tile34, count in enumerate(player.hand.as_tile34()):
            if count > 0:
                mask[tile34] = 1
