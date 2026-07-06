from dataclasses import dataclass, field
from enum import Enum
from typing import TYPE_CHECKING, Optional, Union

if TYPE_CHECKING:
    from .claims import PendingClaim
    from .player import MahjongPlayer
    from .scoring import HandValue
    from .wall import MahjongWall


class DrawSource(str, Enum):
    LIVE = "live"
    RINSHAN = "rinshan"


class ClaimStage(str, Enum):
    RON = "ron"
    KAN_OPEN = "kan_open"
    PON = "pon"
    CHI = "chi"


@dataclass(frozen=True)
class DiscardRef:
    seat: int
    tile34: int


@dataclass(frozen=True)
class TurnPhase:
    seat: int
    draw_source: DrawSource = DrawSource.LIVE


@dataclass(frozen=True)
class RiichiDiscardPhase:
    seat: int
    legal_discards: tuple[int, ...]


@dataclass(frozen=True)
class AfterCallDiscardPhase:
    seat: int


@dataclass(frozen=True)
class AfterKanPhase:
    seat: int


@dataclass
class ClaimPhase:
    discard: DiscardRef
    pending_claims: list["PendingClaim"]
    stage: ClaimStage
    cursor: int = 0
    accepted_ron: list["PendingClaim"] = field(default_factory=list)


@dataclass(frozen=True)
class HandResult:
    kind: str  # "tsumo" or "ron"
    winner: int
    from_seat: Optional[int]
    tile34: int
    score: "HandValue"


@dataclass(frozen=True)
class TerminalPhase:
    last_seat: int
    results: tuple[HandResult, ...]
    reason: str


Phase = Union[
    TurnPhase,
    RiichiDiscardPhase,
    AfterCallDiscardPhase,
    AfterKanPhase,
    ClaimPhase,
    TerminalPhase,
]


@dataclass
class GameState:
    players: list["MahjongPlayer"]
    wall: "MahjongWall"
    phase: Phase
    kan_count: int = 0
    last_discard: Optional[DiscardRef] = None

    @property
    def done(self) -> bool:
        return isinstance(self.phase, TerminalPhase)

    @property
    def actor_seat(self) -> int:
        if isinstance(self.phase, ClaimPhase):
            # The claim helper ensures a current prompt exists while this phase
            # is active.  Keeping the fallback makes debug output safer.
            from .claims import current_claimant

            return current_claimant(self.phase)
        if isinstance(self.phase, TerminalPhase):
            return self.phase.last_seat
        return self.phase.seat

    @property
    def terminal_results(self) -> tuple[HandResult, ...]:
        if isinstance(self.phase, TerminalPhase):
            return self.phase.results
        return ()
