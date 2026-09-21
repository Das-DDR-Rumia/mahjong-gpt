from mahjong.meld import Meld

from .claims import current_offers, same_suit_consecutive
from .constant import ACTIONS_PER_SEAT, NUM_SEATS
from .hand import get_meld_type
from .state import (
    AfterCallDiscardPhase,
    AfterKanPhase,
    ClaimPhase,
    GameState,
    RiichiDiscardPhase,
    TerminalPhase,
    TurnPhase,
)


def assert_integrity(state: GameState) -> None:
    """Raise ``AssertionError`` when tile or phase invariants are broken."""
    counts = [0] * 34
    for player in state.players:
        for tile34, count in enumerate(player.hand.as_tile34()):
            counts[tile34] += count
        for tile34 in player.discards:
            counts[tile34] += 1
        for meld in player.melds:
            for tile136 in meld.tiles:
                counts[tile136 // 4] += 1

    for tile34 in state.wall.all_unseen_tile34_list():
        counts[tile34] += 1

    if sum(counts) != 136:
        raise AssertionError(f"total tile mismatch: {sum(counts)} != 136")
    wrong = [(tile34, count) for tile34, count in enumerate(counts) if count != 4]
    if wrong:
        raise AssertionError(f"tile count mismatch: {wrong}")

    assert len(state.players) == NUM_SEATS
    assert 0 <= state.kan_count <= 4
    assert 0 <= state.actor_seat < NUM_SEATS

    for player in state.players:
        if player.state.riichi_lock:
            assert player.state.riichi
            assert player.state.last_drawn is not None
        if player.state.riichi:
            for meld in player.melds:
                if bool(getattr(meld, "opened", False)):
                    raise AssertionError("riichi hand has an opened meld")

    if isinstance(state.phase, ClaimPhase):
        phase = state.phase
        offers = current_offers(phase)
        assert offers
        assert state.actor_seat == offers[0].claimant
        assert state.players[phase.discard.seat].discards
        assert state.players[phase.discard.seat].discards[-1] == phase.discard.tile34
        for claim in phase.pending_claims:
            assert claim.from_seat == phase.discard.seat
            assert claim.discard_tile34 == phase.discard.tile34
            assert claim.claimant != claim.from_seat
            assert 0 <= claim.action < ACTIONS_PER_SEAT
            if claim.chi_sequence is not None:
                assert phase.discard.tile34 in claim.chi_sequence
                assert same_suit_consecutive(claim.chi_sequence)
            elif claim.action in (34, 35, 36):
                raise AssertionError("chi action without sequence")

    if isinstance(state.phase, TerminalPhase):
        return

    if isinstance(state.phase, ClaimPhase):
        expected_current = None
    elif isinstance(
        state.phase,
        (TurnPhase, RiichiDiscardPhase, AfterCallDiscardPhase, AfterKanPhase),
    ):
        expected_current = state.phase.seat
    else:
        expected_current = None

    for seat, player in enumerate(state.players):
        structural = player.hand.total_tiles() + sum(3 for _ in player.melds)
        physical = player.hand.total_tiles() + sum(
            len(meld.tiles) for meld in player.melds
        )
        kan_melds = sum(
            1 for meld in player.melds if get_meld_type(meld) == Meld.KAN
        )
        assert physical == structural + kan_melds
        expected = 13 if expected_current is None or seat != expected_current else 14
        if structural != expected:
            raise AssertionError(
                f"seat {seat}: structural tile count {structural}, expected {expected}"
            )
