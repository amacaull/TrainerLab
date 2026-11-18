import numpy as np
import pytest

from trainerlab.learning.encoding import ACTIONS, SLOTS, field_names, pokemon_names
from trainerlab.learning.env import VecEnv, random_actions


def test_the_arrays_have_one_entry_per_battle_and_side() -> None:
    obs = VecEnv(3, seed=1).observe()

    assert obs.pokemon.shape == (3, 2, SLOTS, len(pokemon_names()))
    assert obs.ids.shape == (3, 2, SLOTS, 7)
    assert obs.field.shape == (3, 2, len(field_names()))
    assert obs.mask.shape == (3, 2, ACTIONS)


def test_random_play_ends_battles_zero_sum_and_starts_new_ones() -> None:
    env, rng = VecEnv(16, seed=2), np.random.default_rng(0)
    ended, replacements = 0, 0
    for _ in range(300):
        obs = env.observe()
        assert (obs.mask.sum(axis=(1, 2)) > 0).all()  # every battle waits for someone
        # A replacement: one side chooses, among switches only.
        waiting = obs.mask.sum(axis=2) == 0
        for battle, side in zip(*np.nonzero(waiting), strict=True):
            other = obs.mask[battle, 1 - side]
            assert not other[:4].any() and other[4:].any()
            replacements += 1
        rewards, done = env.step(random_actions(obs.mask, rng))
        assert (rewards.sum(axis=1) == 0).all()
        assert (rewards[~done] == 0).all()
        assert set(np.abs(rewards[done]).ravel()) <= {0.0, 1.0}
        ended += int(done.sum())
    assert ended > 0 and env.finished == ended
    assert replacements > 0
    assert env.battles == 16


def test_the_same_seed_and_actions_give_the_same_battles() -> None:
    def run(seed: int) -> list[float]:
        env, rng, seen = VecEnv(4, seed), np.random.default_rng(1), []
        for _ in range(60):
            obs = env.observe()
            rewards, _ = env.step(random_actions(obs.mask, rng))
            seen += list(rewards.ravel()) + list(obs.pokemon.ravel()[:50])
        return seen

    assert run(5) == run(5)
    assert run(5) != run(6)


def test_an_illegal_option_is_refused_and_nothing_moves() -> None:
    env = VecEnv(2, seed=3)
    before = env.observe()
    actions = random_actions(before.mask, np.random.default_rng(0))
    actions[1, 0] = 9 if before.mask[1, 0, 9] == 0 else 42

    with pytest.raises(ValueError, match="not legal"):
        env.step(actions)
    assert np.array_equal(env.observe().pokemon, before.pokemon)
    with pytest.raises(ValueError, match="one action per battle"):
        env.step(np.zeros(3, dtype=np.int32))


def test_a_random_action_is_always_legal() -> None:
    mask = np.array([[[0, 1, 0, 1, 0, 0, 0, 0, 0, 0], [0] * 10]], dtype=np.float32)
    for seed in range(20):
        action = random_actions(mask, np.random.default_rng(seed))
        assert action[0, 0] in (1, 3) and action[0, 1] == 0
