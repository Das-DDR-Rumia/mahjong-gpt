from pydantic import BaseModel, ConfigDict


class RewardConfig(BaseModel):
    model_config = ConfigDict(frozen=True)

    score_weight: float = 1.0
    reward_open_tanyao: float = 0.0
    reward_riichi: float = 0.0
    reward_weight_shanten: float = 1.0
    penalty_ava_num: float = 1.0
