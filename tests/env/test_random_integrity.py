"""
Randomized integrity tests with fixed seed.

Goal:
- ensure no tile ever appears 5 times globally
- ensure total tiles always equals 136
"""

import random

from src.env.env import MahjongEnv

RANDOM_TEST_N = 65536
RANDOM_TEST_SEED = 124


def pick_random_legal_action(rng: random.Random, action_mask):
    legal = [i for i, v in enumerate(action_mask) if v == 1]
    return rng.choice(legal)


def test_random_play_integrity():
    rng = random.Random(RANDOM_TEST_SEED)
    env = MahjongEnv(seed=RANDOM_TEST_SEED)
    obs, reward, done, info = env.reset()

    # run multiple steps; reset when done
    steps = 4096
    for _ in range(RANDOM_TEST_N):
        for _ in range(steps):
            env.assert_integrity()

            action = pick_random_legal_action(rng, info["action_mask"])
            obs, reward, done, info = env.step(action)

            env.assert_integrity()

            if done:
                break
