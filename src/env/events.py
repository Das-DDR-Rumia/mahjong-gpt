from dataclasses import dataclass
from typing import Any, Mapping

from .scoring import HandValue


class DomainEvent:
    @property
    def name(self) -> str:
        raise NotImplementedError

    @property
    def payload(self) -> Mapping[str, Any]:
        raise NotImplementedError


@dataclass(frozen=True)
class TileDrawn(DomainEvent):
    seat: int
    tile34: int
    source: str

    @property
    def name(self) -> str:
        return "tile_drawn"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {"seat": self.seat, "tile34": self.tile34, "source": self.source}


@dataclass(frozen=True)
class TileDiscarded(DomainEvent):
    seat: int
    tile34: int

    @property
    def name(self) -> str:
        return "tile_discarded"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {"seat": self.seat, "tile34": self.tile34}


@dataclass(frozen=True)
class RiichiDeclared(DomainEvent):
    seat: int

    @property
    def name(self) -> str:
        return "riichi_declared"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {"seat": self.seat}


@dataclass(frozen=True)
class MeldMade(DomainEvent):
    seat: int
    kind: str
    tiles34: tuple[int, ...]

    @property
    def name(self) -> str:
        return "meld_made"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {"seat": self.seat, "kind": self.kind, "tiles34": list(self.tiles34)}


@dataclass(frozen=True)
class MeldUpgraded(DomainEvent):
    seat: int
    tile34: int

    @property
    def name(self) -> str:
        return "meld_upgraded"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {"seat": self.seat, "kind": "kakan", "tile34": self.tile34}


@dataclass(frozen=True)
class WinRon(DomainEvent):
    seat: int
    from_seat: int
    tile34: int
    score: HandValue

    @property
    def name(self) -> str:
        return "win_ron"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {
            "seat": self.seat,
            "from_seat": self.from_seat,
            "tile34": self.tile34,
            "score": self.score,
        }


@dataclass(frozen=True)
class WinTsumo(DomainEvent):
    seat: int
    tile34: int
    score: HandValue

    @property
    def name(self) -> str:
        return "win_tsumo"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {"seat": self.seat, "tile34": self.tile34, "score": self.score}


@dataclass(frozen=True)
class NoYakuEncountered(DomainEvent):
    discarder: int
    claimant: int
    tile34: int

    @property
    def name(self) -> str:
        return "no_yaku_encountered"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {
            "discarder": self.discarder,
            "claimant": self.claimant,
            "tile34": self.tile34,
        }


@dataclass(frozen=True)
class ClaimsGenerated(DomainEvent):
    discarder: int
    tile34: int
    claims: tuple[tuple[int, int], ...]

    @property
    def name(self) -> str:
        return "claims_generated"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {
            "discarder": self.discarder,
            "tile34": self.tile34,
            "claims": [
                {"claimant": claimant, "action": action}
                for claimant, action in self.claims
            ],
        }


@dataclass(frozen=True)
class WallExhausted(DomainEvent):
    seat: int

    @property
    def name(self) -> str:
        return "wall_exhausted"

    @property
    def payload(self) -> Mapping[str, Any]:
        return {"seat": self.seat}
