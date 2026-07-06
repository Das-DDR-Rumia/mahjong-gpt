from mahjong.constants import EAST, NORTH, SOUTH, WEST

# Table / action-space dimensions
ACTIONS_PER_SEAT = 46
NUM_SEATS = 4

# Local action ids: 0..33 are tile34 discards.
DISCARD_MIN = 0
DISCARD_MAX = 33

CHI_UP = 34
CHI_MID = 35
CHI_DOWN = 36
PON = 37
KAN_OPEN = 38
KAN_ADD = 39
KAN_CLOSED = 40
PEI = 41  # Reserved for three-player Mahjong; intentionally unsupported here.
RIICHI = 42
RON = 43
TSUMO = 44
PASS = 45

ACTION_MIN = DISCARD_MIN
ACTION_MAX = PASS
SPEC_MIN = CHI_UP
SPEC_MAX = PASS

# Token vocabulary layout.
HAND_MIN = 46
HAND_MAX = 79

PLAYER0 = 80
PLAYER1 = 81
PLAYER2 = 82
PLAYER3 = 83
PLAYER_TOKENS = (PLAYER0, PLAYER1, PLAYER2, PLAYER3)
PLAYER_MIN = PLAYER0
PLAYER_MAX = PLAYER3

SEP_ID = 84
PAD_ID = 85
VOCAB_SIZE = PAD_ID + 1

# Seat -> player-wind mapping for the single-hand environment.
WIND_IDX_MAP = {
    0: EAST,
    1: SOUTH,
    2: WEST,
    3: NORTH,
}

CHI_ACTIONS = (CHI_UP, CHI_MID, CHI_DOWN)
KAN_ACTIONS = (KAN_OPEN, KAN_ADD, KAN_CLOSED)
DISCARD_ACTIONS = range(DISCARD_MIN, DISCARD_MAX + 1)

# Priority is intentionally the original simplified priority model.  It is
# used only to choose the next claim stage; this project does not add furiten,
# abortive draws, chankan, or match-level rules.
CLAIM_PRIORITY = {
    RON: 0,
    KAN_OPEN: 1,
    PON: 2,
    CHI_UP: 3,
    CHI_MID: 3,
    CHI_DOWN: 3,
}
