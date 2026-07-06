from dataclasses import dataclass
from typing import Optional

from mahjong.shanten import Shanten

from .claims import (
    ClaimBuilder,
    PendingClaim,
    advance_after_pass,
    advance_after_ron,
    current_offers,
    make_claim_phase,
)
from .constant import (
    CHI_DOWN,
    CHI_MID,
    CHI_UP,
    DISCARD_MAX,
    DISCARD_MIN,
    KAN_ADD,
    KAN_CLOSED,
    KAN_OPEN,
    PASS,
    PON,
    RIICHI,
    RON,
    TSUMO,
)
from .errors import IllegalActionError
from .events import (
    DomainEvent,
    MeldMade,
    MeldUpgraded,
    RiichiDeclared,
    TileDiscarded,
    TileDrawn,
    WallExhausted,
    WinRon,
    WinTsumo,
)
from .legal import LegalActionAnalyzer, LegalDecision
from .player import MahjongPlayer
from .scoring import ScoringService
from .state import (
    AfterCallDiscardPhase,
    AfterKanPhase,
    ClaimPhase,
    DiscardRef,
    DrawSource,
    GameState,
    HandResult,
    RiichiDiscardPhase,
    TerminalPhase,
    TurnPhase,
)
from .wall import MahjongWall


@dataclass(frozen=True)
class Transition:
    actor: int
    action: int
    events: tuple[DomainEvent, ...]


class MahjongEngine:
    """Mutates one ``GameState`` only after a legal action is established."""

    def __init__(self, shanten_calc: Shanten, scoring: ScoringService) -> None:
        self.shanten_calc = shanten_calc
        self.scoring = scoring
        self.claim_builder = ClaimBuilder(shanten_calc, scoring)
        self.legal = LegalActionAnalyzer(shanten_calc, scoring)

    def new_hand(
        self,
        wall: MahjongWall,
        players: list[MahjongPlayer],
    ) -> tuple[GameState, tuple[DomainEvent, ...]]:
        wall.reset()
        for player in players:
            player.reset()

        events: list[DomainEvent] = []
        for _ in range(13):
            for player in players:
                tile = wall.draw()
                player.draw(tile)
                events.append(TileDrawn(player.seat, tile, DrawSource.LIVE.value))

        dealer_tile = wall.draw()
        players[0].draw(dealer_tile)
        events.append(TileDrawn(0, dealer_tile, DrawSource.LIVE.value))

        return (
            GameState(players=players, wall=wall, phase=TurnPhase(seat=0)),
            tuple(events),
        )

    def step(
        self,
        state: GameState,
        action: int,
        decision: Optional[LegalDecision] = None,
    ) -> Transition:
        if state.done:
            return Transition(actor=state.actor_seat, action=action, events=())

        decision = decision or self.legal.analyze(state)
        if not decision.is_legal(action):
            raise IllegalActionError(
                f"illegal action {action} for seat {state.actor_seat}; "
                f"mask={list(decision.mask)}"
            )

        actor = state.actor_seat
        if isinstance(state.phase, ClaimPhase):
            events = self._apply_claim_action(state, action, decision)
        elif isinstance(state.phase, TurnPhase):
            events = self._apply_turn_action(state, action, decision)
        elif isinstance(state.phase, RiichiDiscardPhase):
            events = self._apply_discard(state, actor, action)
        elif isinstance(state.phase, AfterCallDiscardPhase):
            events = self._apply_discard(state, actor, action)
        elif isinstance(state.phase, AfterKanPhase):
            if action == TSUMO:
                events = self._apply_tsumo(state, actor, decision)
            else:
                events = self._apply_discard(state, actor, action)
        else:
            raise RuntimeError(f"unsupported phase {state.phase!r}")
        return Transition(actor=actor, action=action, events=tuple(events))

    def _apply_turn_action(
        self,
        state: GameState,
        action: int,
        decision: LegalDecision,
    ) -> list[DomainEvent]:
        seat = state.actor_seat
        if DISCARD_MIN <= action <= DISCARD_MAX:
            return self._apply_discard(state, seat, action)
        if action == TSUMO:
            return self._apply_tsumo(state, seat, decision)
        if action == RIICHI:
            player = state.players[seat]
            player.state.riichi = True
            player.state.riichi_lock = False
            legal_discards = self.legal.riichi_discard_tiles(state, seat)
            state.phase = RiichiDiscardPhase(seat=seat, legal_discards=legal_discards)
            return [RiichiDeclared(seat)]
        if action == KAN_CLOSED:
            if decision.closed_kan_tile is None:
                raise RuntimeError("legal ankan action missing selected tile")
            return self._apply_closed_kan(state, seat, decision.closed_kan_tile)
        if action == KAN_ADD:
            if decision.added_kan_tile is None:
                raise RuntimeError("legal kakan action missing selected tile")
            return self._apply_added_kan(state, seat, decision.added_kan_tile)
        raise IllegalActionError(f"unsupported turn action: {action}")

    def _apply_discard(
        self, state: GameState, seat: int, tile34: int
    ) -> list[DomainEvent]:
        player = state.players[seat]
        if player.state.riichi and player.state.riichi_lock:
            if player.state.last_drawn is None or tile34 != player.state.last_drawn:
                raise IllegalActionError(
                    "riichi lock requires discarding the drawn tile"
                )

        player.discard(tile34)
        if player.state.riichi and not player.state.riichi_lock:
            player.state.riichi_lock = True

        discard = DiscardRef(seat=seat, tile34=tile34)
        state.last_discard = discard
        events: list[DomainEvent] = [TileDiscarded(seat=seat, tile34=tile34)]
        build = self.claim_builder.build(state, seat, tile34)
        events.extend(build.events)

        if build.claims:
            state.phase = make_claim_phase(discard, build.claims)
        else:
            events.extend(self._advance_to_next_draw(state, seat))
        return events

    def _apply_tsumo(
        self,
        state: GameState,
        seat: int,
        decision: LegalDecision,
    ) -> list[DomainEvent]:
        score = decision.tsumo_score or self.scoring.estimate_tsumo(state, seat)
        if not score.is_valid:
            raise IllegalActionError(f"tsumo invalid: {score.error}")
        tile34 = state.players[seat].state.last_drawn
        if tile34 is None:
            raise RuntimeError("tsumo without last drawn tile")
        result = HandResult("tsumo", seat, None, tile34, score)
        state.phase = TerminalPhase(last_seat=seat, results=(result,), reason="tsumo")
        return [WinTsumo(seat=seat, tile34=tile34, score=score)]

    def _apply_closed_kan(
        self,
        state: GameState,
        seat: int,
        tile34: int,
    ) -> list[DomainEvent]:
        player = state.players[seat]
        player.hand.remove(tile34, 4)
        player.hand.add_closed_kan(tile34)
        state.kan_count += 1
        replacement = state.wall.draw_rinshan()
        player.draw(replacement)
        state.phase = AfterKanPhase(seat=seat)
        return [
            MeldMade(seat=seat, kind="ankan", tiles34=(tile34,) * 4),
            TileDrawn(seat=seat, tile34=replacement, source=DrawSource.RINSHAN.value),
        ]

    def _apply_added_kan(
        self,
        state: GameState,
        seat: int,
        tile34: int,
    ) -> list[DomainEvent]:
        player = state.players[seat]
        player.hand.remove(tile34)
        player.hand.upgrade_pon_to_kan(tile34)
        state.kan_count += 1
        replacement = state.wall.draw_rinshan()
        player.draw(replacement)
        state.phase = AfterKanPhase(seat=seat)
        return [
            MeldUpgraded(seat=seat, tile34=tile34),
            TileDrawn(seat=seat, tile34=replacement, source=DrawSource.RINSHAN.value),
        ]

    def _apply_claim_action(
        self,
        state: GameState,
        action: int,
        decision: LegalDecision,
    ) -> list[DomainEvent]:
        phase = state.phase
        assert isinstance(phase, ClaimPhase)
        claimant = state.actor_seat

        if action == PASS:
            if phase.accepted_ron:
                # Once somebody has called ron, only remaining ron candidates
                # are prompted; lower calls are already suppressed.
                if advance_after_ron(phase):
                    return []
                return self._finish_ron_resolution(state, claimant, phase)
            if advance_after_pass(phase):
                return []
            return self._advance_to_next_draw(state, phase.discard.seat)

        chosen = decision.claim_by_action.get(action)
        if chosen is None or chosen.claimant != claimant:
            raise IllegalActionError("claim action is not offered to current claimant")

        if action == RON:
            if chosen.score_result is None:
                raise RuntimeError("ron claim missing score")
            phase.accepted_ron.append(chosen)
            event = WinRon(
                seat=claimant,
                from_seat=chosen.from_seat,
                tile34=chosen.discard_tile34,
                score=chosen.score_result,
            )
            if advance_after_ron(phase):
                return [event]
            return [event, *self._finish_ron_resolution(state, claimant, phase)]

        if action == KAN_OPEN:
            return self._apply_open_kan_claim(state, chosen)
        if action == PON:
            return self._apply_pon_claim(state, chosen)
        if action in (CHI_UP, CHI_MID, CHI_DOWN):
            return self._apply_chi_claim(state, chosen)
        raise IllegalActionError(f"unsupported claim action: {action}")

    def _finish_ron_resolution(
        self,
        state: GameState,
        last_seat: int,
        phase: ClaimPhase,
    ) -> list[DomainEvent]:
        results = tuple(
            HandResult(
                kind="ron",
                winner=claim.claimant,
                from_seat=claim.from_seat,
                tile34=claim.discard_tile34,
                score=claim.score_result,  # type: ignore[arg-type]
            )
            for claim in phase.accepted_ron
        )
        state.phase = TerminalPhase(last_seat=last_seat, results=results, reason="ron")
        return []

    def _apply_chi_claim(
        self, state: GameState, claim: PendingClaim
    ) -> list[DomainEvent]:
        if claim.chi_sequence is None:
            raise RuntimeError("chi claim missing sequence")
        discarder = state.players[claim.from_seat]
        claimant = state.players[claim.claimant]
        discarder.pop_last_discard_for_call(claim.discard_tile34)
        remove = [tile for tile in claim.chi_sequence if tile != claim.discard_tile34]
        if len(remove) != 2:
            raise RuntimeError("invalid chi sequence")
        claimant.hand.remove(remove[0])
        claimant.hand.remove(remove[1])
        claimant.hand.add_open_meld_chi(claim.chi_sequence)
        state.phase = AfterCallDiscardPhase(seat=claim.claimant)
        return [MeldMade(claim.claimant, "chi", tuple(claim.chi_sequence))]

    def _apply_pon_claim(
        self, state: GameState, claim: PendingClaim
    ) -> list[DomainEvent]:
        discarder = state.players[claim.from_seat]
        claimant = state.players[claim.claimant]
        discarder.pop_last_discard_for_call(claim.discard_tile34)
        claimant.hand.remove(claim.discard_tile34, 2)
        claimant.hand.add_open_meld_pon(claim.discard_tile34)
        state.phase = AfterCallDiscardPhase(seat=claim.claimant)
        return [MeldMade(claim.claimant, "pon", (claim.discard_tile34,) * 3)]

    def _apply_open_kan_claim(
        self, state: GameState, claim: PendingClaim
    ) -> list[DomainEvent]:
        discarder = state.players[claim.from_seat]
        claimant = state.players[claim.claimant]
        discarder.pop_last_discard_for_call(claim.discard_tile34)
        claimant.hand.remove(claim.discard_tile34, 3)
        claimant.hand.add_open_meld_kan(claim.discard_tile34)
        state.kan_count += 1
        replacement = state.wall.draw_rinshan()
        claimant.draw(replacement)
        state.phase = AfterKanPhase(seat=claim.claimant)
        return [
            MeldMade(claim.claimant, "minkan", (claim.discard_tile34,) * 4),
            TileDrawn(claim.claimant, replacement, DrawSource.RINSHAN.value),
        ]

    def _advance_to_next_draw(
        self, state: GameState, from_seat: int
    ) -> list[DomainEvent]:
        if state.wall.remaining_live() <= 0:
            state.phase = TerminalPhase(
                last_seat=from_seat, results=(), reason="wall_exhausted"
            )
            return [WallExhausted(seat=from_seat)]
        seat = (from_seat + 1) % 4
        tile = state.wall.draw()
        state.players[seat].draw(tile)
        state.phase = TurnPhase(seat=seat, draw_source=DrawSource.LIVE)
        return [TileDrawn(seat=seat, tile34=tile, source=DrawSource.LIVE.value)]
