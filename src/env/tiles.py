from dataclasses import dataclass
from typing import Iterable, Tuple

from .constant import CHI_DOWN, CHI_MID, CHI_UP


def tile34_to_str(tile34: int) -> str:
    """Convert tile34 index (0..33) to compact notation such as ``1m``."""
    if not 0 <= tile34 <= 33:
        raise ValueError(f"tile34 out of range: {tile34}")
    if tile34 <= 26:
        suit = tile34 // 9
        num = tile34 % 9 + 1
        return f"{num}{'mps'[suit]}"
    return f"{tile34 - 27 + 1}z"


def tile34_from_str(tile: str) -> int:
    """Convert compact notation such as ``5p`` to a tile34 index."""
    if len(tile) != 2:
        raise ValueError(f"invalid tile string: {tile}")
    try:
        num = int(tile[0])
    except ValueError as exc:
        raise ValueError(f"invalid tile string: {tile}") from exc
    suit = tile[1]
    if suit in ("m", "p", "s"):
        if not 1 <= num <= 9:
            raise ValueError(f"invalid suit tile: {tile}")
        return {"m": 0, "p": 9, "s": 18}[suit] + num - 1
    if suit == "z":
        if not 1 <= num <= 7:
            raise ValueError(f"invalid honor tile: {tile}")
        return 27 + num - 1
    raise ValueError(f"invalid tile string: {tile}")


def is_suit(tile34: int) -> bool:
    return 0 <= tile34 <= 26


def suit_base(tile34: int) -> int:
    if not is_suit(tile34):
        raise ValueError(f"not a suited tile: {tile34}")
    return tile34 // 9 * 9


def iter_chi_sequences_containing(tile34: int) -> Iterable[Tuple[int, int, int]]:
    """Yield valid same-suit 3-tile runs containing ``tile34``."""
    if not is_suit(tile34):
        return
    base = suit_base(tile34)
    offset = tile34 - base
    for start in (offset - 2, offset - 1, offset):
        if 0 <= start <= 6:
            yield (base + start, base + start + 1, base + start + 2)


@dataclass(frozen=True)
class ChiOption:
    action: int
    sequence: Tuple[int, int, int]
    need_remove: Tuple[int, int]


def chi_option_from_sequence(
    discard_tile34: int,
    sequence: Tuple[int, int, int],
) -> ChiOption:
    """Map a chi sequence to the legacy action id and removed tiles."""
    if discard_tile34 not in sequence:
        raise ValueError("discard not in chi sequence")
    if discard_tile34 == sequence[0]:
        return ChiOption(CHI_UP, sequence, (sequence[1], sequence[2]))
    if discard_tile34 == sequence[1]:
        return ChiOption(CHI_MID, sequence, (sequence[0], sequence[2]))
    return ChiOption(CHI_DOWN, sequence, (sequence[0], sequence[1]))
