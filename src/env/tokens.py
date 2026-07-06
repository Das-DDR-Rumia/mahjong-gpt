from collections.abc import Iterable, Iterator
from typing import Any

from pydantic import BaseModel, Field, model_serializer, model_validator

from .constant import (
    DISCARD_MAX,
    DISCARD_MIN,
    HAND_MAX,
    HAND_MIN,
    PAD_ID,
    PLAYER_MAX,
    PLAYER_MIN,
    SEP_ID,
    SPEC_MAX,
    SPEC_MIN,
)
from .tiles import tile34_from_str, tile34_to_str

PLAYER_NAMES = ("P0", "P1", "P2", "P3")
SPEC_MAP = (
    "CHI_UP",
    "CHI_MID",
    "CHI_DOWN",
    "PON",
    "KAN_OPEN",
    "KAN_ADD",
    "KAN_CLOSED",
    "PEI",
    "RIICHI",
    "RON",
    "TSUMO",
    "PASS",
)


class TokenList(BaseModel):
    """Token ids with the original human-readable Pydantic serialization."""

    token_ids: list[int] = Field(default_factory=list)

    @model_serializer(mode="plain")
    def serialize(self) -> str:
        return self.to_human()

    @model_validator(mode="before")
    @classmethod
    def validate_input(cls, data: Any) -> dict[str, Any]:
        if isinstance(data, TokenList):
            return {"token_ids": data.token_ids.copy()}
        if isinstance(data, dict):
            return data
        if isinstance(data, str):
            return {"token_ids": cls._parse_human(data)}
        if isinstance(data, (list, tuple)):
            ids: list[int] = []
            for item in data:
                if isinstance(item, int):
                    ids.append(item)
                elif isinstance(item, dict) and isinstance(item.get("token_id"), int):
                    ids.append(item["token_id"])
                else:
                    raise ValueError(f"invalid token item: {item!r}")
            return {"token_ids": ids}
        raise ValueError(f"cannot convert {type(data)!r} to TokenList")

    @classmethod
    def _parse_human(cls, text: str) -> list[int]:
        ids: list[int] = []
        parts = text.replace("<SEP>", " <SEP> ").replace("<PAD>", " <PAD> ").split()
        for part in parts:
            if (
                part in ("<SEP>", "<PAD>")
                or part.startswith("[")
                or part.startswith("D")
            ):
                ids.append(cls._parse_token(part))
            elif part.startswith("<P") and part.endswith(">"):
                ids.append(cls._parse_token(part))
            else:
                ids.extend(cls._parse_tiles(part))
        return ids

    @staticmethod
    def _parse_token(token: str) -> int:
        if token == "<PAD>":
            return PAD_ID
        if token == "<SEP>":
            return SEP_ID
        if token.startswith("<P") and token.endswith(">"):
            index = int(token[2:-1])
            if not 0 <= index < 4:
                raise ValueError(f"invalid player token: {token}")
            return PLAYER_MIN + index
        if token.startswith("D"):
            return DISCARD_MIN + tile34_from_str(token[1:])
        if token.startswith("[") and token.endswith("]"):
            name = token[1:-1]
            try:
                return SPEC_MIN + SPEC_MAP.index(name)
            except ValueError as exc:
                raise ValueError(f"unknown special token: {token}") from exc
        return HAND_MIN + tile34_from_str(token)

    @staticmethod
    def _parse_tiles(text: str) -> list[int]:
        ids: list[int] = []
        index = 0
        while index < len(text):
            if index + 1 < len(text) and text[index + 1] in "mpsz":
                ids.append(HAND_MIN + tile34_from_str(text[index : index + 2]))
                index += 2
            else:
                raise ValueError(f"cannot parse token sequence near: {text[index:]!r}")
        return ids

    def to_ids(self) -> list[int]:
        return self.token_ids.copy()

    def to_human(self) -> str:
        parts: list[str] = []
        pending_tiles: list[str] = []

        def flush_tiles() -> None:
            if pending_tiles:
                parts.append("".join(pending_tiles))
                pending_tiles.clear()

        for token_id in self.token_ids:
            if token_id == SEP_ID:
                flush_tiles()
                parts.append("<SEP>")
            elif HAND_MIN <= token_id <= HAND_MAX:
                pending_tiles.append(tile34_to_str(token_id - HAND_MIN))
            else:
                flush_tiles()
                parts.append(self._token_to_human(token_id))
        flush_tiles()
        return " ".join(parts)

    @staticmethod
    def _token_to_human(token_id: int) -> str:
        if token_id == PAD_ID:
            return "<PAD>"
        if token_id == SEP_ID:
            return "<SEP>"
        if PLAYER_MIN <= token_id <= PLAYER_MAX:
            return f"<{PLAYER_NAMES[token_id - PLAYER_MIN]}>"
        if DISCARD_MIN <= token_id <= DISCARD_MAX:
            return f"D{tile34_to_str(token_id - DISCARD_MIN)}"
        if SPEC_MIN <= token_id <= SPEC_MAX:
            return f"[{SPEC_MAP[token_id - SPEC_MIN]}]"
        raise ValueError(f"token id outside vocabulary: {token_id}")

    @classmethod
    def from_human(cls, text: str) -> "TokenList":
        return cls.model_validate(text)

    @classmethod
    def empty(cls) -> "TokenList":
        return cls()

    @classmethod
    def from_ids(cls, ids: list[int]) -> "TokenList":
        return cls(token_ids=ids.copy())

    def append(self, token_id: int) -> None:
        self.token_ids.append(token_id)

    def extend(self, token_ids: Iterable[int]) -> None:
        self.token_ids.extend(token_ids)

    def copy(self) -> "TokenList":
        return TokenList(token_ids=self.token_ids.copy())

    def get_segments(self) -> list[list[int]]:
        segments: list[list[int]] = []
        current: list[int] = []
        for token_id in self.token_ids:
            if token_id == SEP_ID:
                if current:
                    segments.append(current)
                    current = []
            else:
                current.append(token_id)
        if current:
            segments.append(current)
        return segments

    def get_player_segment(self, player_id: int) -> list[int]:
        segments = self.get_segments()
        return segments[player_id] if 0 <= player_id < len(segments) else []

    def __len__(self) -> int:
        return len(self.token_ids)

    def __getitem__(self, index: int | slice) -> int | list[int]:
        return self.token_ids[index]

    def __iter__(self) -> Iterator[int]:
        return iter(self.token_ids)

    def __contains__(self, item: int) -> bool:
        return item in self.token_ids

    def __repr__(self) -> str:
        return f"TokenList({self.to_human()})"

    def __str__(self) -> str:
        return self.to_human()
