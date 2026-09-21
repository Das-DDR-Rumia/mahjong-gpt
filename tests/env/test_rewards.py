from types import SimpleNamespace

import pytest
from mahjong.constants import EAST, SOUTH

from src.env.events import MeldMade, WinRon, WinTsumo
from src.env.env import MahjongEnv
from src.env.rewards import DiscardSnapshot, RewardProjector
from src.env.scoring import HandValue


@pytest.fixture
def projector():
    config = SimpleNamespace(
        reward_weight_shanten=30.0,
        penalty_ava_num=1.2,
        score_weight=1.0,
        reward_riichi=0.0,
        reward_open_tanyao=-5.0,
        reward_confirmed_yaku_han=5.0,
        reward_meld_cap=20.0,
    )
    return RewardProjector(config, shanten_calc=None)


def test_same_shanten_rewards_more_available_improvement_tiles(projector):
    projector.project(
        actor=0,
        events=(),
        discard_snapshot=DiscardSnapshot(seat=0, shanten=2, available=4),
    )

    improved = projector.project(
        actor=0,
        events=(),
        discard_snapshot=DiscardSnapshot(seat=0, shanten=2, available=8),
    )
    worsened = projector.project(
        actor=0,
        events=(),
        discard_snapshot=DiscardSnapshot(seat=0, shanten=2, available=5),
    )

    assert improved.reward_update == 4.0
    assert worsened.reward_update == pytest.approx(-3.6)


def test_ordinary_open_meld_has_no_flat_penalty(projector):
    result = projector.project(
        actor=1,
        events=(MeldMade(seat=1, kind="pon", tiles34=(4, 4, 4)),),
        discard_snapshot=None,
    )

    assert result.reward == 0.0


def test_value_honor_pon_rewards_one_confirmed_han(projector):
    state = SimpleNamespace(
        players=[
            SimpleNamespace(player_wind=EAST),
            SimpleNamespace(player_wind=SOUTH),
            SimpleNamespace(player_wind=29),
            SimpleNamespace(player_wind=30),
        ]
    )

    result = projector.project(
        actor=1,
        events=(MeldMade(seat=1, kind="pon", tiles34=(31, 31, 31)),),
        discard_snapshot=None,
        state=state,
    )

    assert result.reward == 5.0


def test_double_east_pon_rewards_two_han_without_exceeding_cap():
    config = SimpleNamespace(
        reward_confirmed_yaku_han=15.0,
        reward_meld_cap=20.0,
    )
    projector = RewardProjector(config, shanten_calc=None)
    state = SimpleNamespace(
        players=[
            SimpleNamespace(player_wind=EAST),
            SimpleNamespace(player_wind=SOUTH),
            SimpleNamespace(player_wind=29),
            SimpleNamespace(player_wind=30),
        ]
    )

    result = projector.project(
        actor=0,
        events=(MeldMade(seat=0, kind="pon", tiles34=(EAST, EAST, EAST)),),
        discard_snapshot=None,
        state=state,
    )

    assert result.reward == 20.0


def test_ron_reward_is_zero_sum_between_winner_and_discarder(projector):
    score = HandValue(error=None, cost={"main": 8000.0})

    result = projector.project(
        actor=2,
        events=(WinRon(seat=2, from_seat=1, tile34=5, score=score),),
        discard_snapshot=None,
    )

    assert result.reward == 8000.0
    assert result.seat_rewards == (0.0, -8000.0, 8000.0, 0.0)
    assert sum(result.seat_rewards) == 0.0


def test_nondealer_tsumo_reward_charges_each_opponent(projector):
    score = HandValue(
        error=None,
        cost={"main": 2000.0, "additional": 1000.0},
    )

    result = projector.project(
        actor=2,
        events=(WinTsumo(seat=2, tile34=5, score=score),),
        discard_snapshot=None,
    )

    assert result.reward == 4000.0
    assert result.seat_rewards == (-2000.0, -1000.0, 4000.0, -1000.0)
    assert sum(result.seat_rewards) == 0.0


def test_dealer_tsumo_reward_charges_three_equal_payments(projector):
    score = HandValue(
        error=None,
        cost={"main": 2000.0, "additional": 0.0},
    )

    result = projector.project(
        actor=0,
        events=(WinTsumo(seat=0, tile34=5, score=score),),
        discard_snapshot=None,
    )

    assert result.reward == 6000.0
    assert result.seat_rewards == (6000.0, -2000.0, -2000.0, -2000.0)
    assert sum(result.seat_rewards) == 0.0


def test_environment_step_exposes_rewards_for_all_seats():
    env = MahjongEnv(seed=7)
    _, _, _, info = env.reset()
    action = next(index for index, legal in enumerate(info["action_mask"]) if legal)

    _, _, _, next_info = env.step(action)

    assert next_info["seat_rewards"] == [0.0, 0.0, 0.0, 0.0]
