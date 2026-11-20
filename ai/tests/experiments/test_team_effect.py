import math
from collections.abc import Callable

import numpy as np
import pytest

from trainerlab.experiments import teams
from trainerlab.experiments.tournament import wilson_interval
from trainerlab.game.environment import random_team_pair


def _battles(
    first: int, pairs: int, points_of: Callable[[int, int], float]
) -> tuple[np.ndarray, np.ndarray]:
    seeds = np.repeat(np.arange(first, first + pairs), 2)
    return seeds, np.array([points_of(int(s), i % 2) for i, s in enumerate(seeds)], float)


def test_teams_that_decide_are_seen_in_every_measure() -> None:
    # The first team wins both battles whenever it holds the strongest species in the draw.
    def strong_first(seed: int, _: int) -> float:
        team0, team1 = random_team_pair(seed)
        return 1.0 if sorted(team0)[0] < sorted(team1)[0] else 0.0

    result = teams.analyze(*_battles(0, 400, strong_first))

    assert result.battles == 800 and result.decided_pairs == 400
    assert result.agreement == 1.0 and result.agreement_low > 0.9
    assert result.accuracy > 0.6 and result.log_loss < math.log(2)
    assert result.species[0].score > 0.5 > result.species[-1].score
    assert result.species[0].low <= result.species[0].score <= result.species[0].high


def test_coin_flips_agree_half_the_time_and_draws_are_left_out() -> None:
    rng = np.random.default_rng(0)
    seeds, points = _battles(0, 600, lambda seed, replay: float(rng.integers(2)))
    points[:2] = 0.5  # a pair with a draw

    result = teams.analyze(seeds, points)

    assert result.decided_pairs == 599
    assert result.agreement_low < 0.5 < result.agreement_high


def test_a_pair_replays_one_matchup_with_dice_of_its_own() -> None:
    points = teams.play_block("hitter", "hitter", range(190000, 190003))

    assert len(points) == 6
    assert set(points) <= {0.0, 0.5, 1.0}


def test_record_lists_two_battles_per_seed_in_order() -> None:
    seeds, points = teams.record("random", range(10, 13), jobs=1)

    assert seeds.tolist() == [10, 10, 11, 11, 12, 12]
    assert len(points) == 6


def test_a_pair_plays_the_matchup_once_each_way_with_dice_and_bots_of_its_own(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    built: list[tuple[list[str], list[str], int]] = []
    bots: list[int] = []
    winners = iter([0, 1, None, 0])

    class Env:
        def __init__(self, first: list[str], second: list[str], *, seed: int) -> None:
            built.append((first, second, seed))

    monkeypatch.setattr(teams, "BattleEnvironment", Env)

    def make(seed: int) -> int:
        bots.append(seed)
        return seed

    monkeypatch.setattr(teams, "factory", lambda bot: make)
    monkeypatch.setattr(teams, "play_battle", lambda env, b0, b1: next(winners))

    points = teams.play_block("any", "any", range(7, 9))

    t0, t1 = random_team_pair(7)
    assert built[:2] == [(t0, t1, 14), (t1, t0, 15)]  # the teams swap sides, the dice change
    assert [seed for *_, seed in built] == [14, 15, 16, 17]
    assert bots == [28, 29, 30, 31, 32, 33, 34, 35]
    # The first team's points: it wins on side 0, then on side 1; a draw is half a point.
    assert points == [1.0, 1.0, 0.5, 0.0]


def test_the_analysis_gives_the_exact_figures_of_hand_made_battles() -> None:
    # Pairs 10 to 15: (1, 1) won twice by the first team, (0, 0) twice by the second,
    # (1, 0) split, (0.5, 1) and (1, 0.5) with a draw: left out of the agreement.
    seeds = np.repeat(np.arange(10, 15), 2)
    points = np.array([1.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.5, 1.0, 1.0, 0.5])

    result = teams.analyze(seeds, points)

    assert result.decided_pairs == 3 and result.agreement == pytest.approx(2 / 3)
    low, high = wilson_interval(2, 3)
    assert (result.agreement_low, result.agreement_high) == (low, high)
    names, x = teams._compositions(seeds)
    for s in result.species:
        k = names.index(s.name)
        mine, theirs = x[:, k] == 1, x[:, k] == -1
        battles = int(mine.sum() + theirs.sum())
        assert s.battles == battles
        if battles == 0:
            assert math.isnan(s.score)  # not in any of these teams
            continue
        expected = (points[mine].sum() + (1 - points[theirs]).sum()) / battles
        assert s.score == pytest.approx(expected)


def test_the_regression_is_scored_on_the_pairs_whose_seed_is_a_multiple_of_5() -> None:
    seeds = np.repeat(np.arange(0, 400), 2)
    rng = np.random.default_rng(1)
    points = rng.integers(0, 2, len(seeds)).astype(float)

    result = teams.analyze(seeds, points)

    names, x = teams._compositions(seeds)
    test = seeds % 5 == 0
    w = teams._fit(x[~test], points[~test])
    p = 1 / (1 + np.exp(-x[test] @ w))
    y = points[test]
    expected = -(y * np.log(p + 1e-12) + (1 - y) * np.log(1 - p + 1e-12)).mean()
    assert result.log_loss == pytest.approx(expected, rel=1e-9)
    assert result.accuracy == pytest.approx(((p > 0.5) == (y == 1.0)).mean())


def test_with_only_draws_held_out_no_winner_is_called() -> None:
    seeds = np.repeat(np.arange(0, 10), 2)
    points = np.where(seeds % 5 == 0, 0.5, 1.0)
    assert teams.analyze(seeds, points).accuracy == 0.0


def test_a_composition_counts_each_side_s_species_with_its_sign() -> None:
    names, x = teams._compositions(np.array([3, 4]))
    for row, seed in enumerate((3, 4)):
        t0, t1 = random_team_pair(seed)
        expected = np.zeros(len(names))
        for name in t0:
            expected[names.index(name)] += 1
        for name in t1:
            expected[names.index(name)] -= 1
        assert (x[row] == expected).all()


def test_the_regression_finds_its_optimum_and_the_strengths_it_was_made_with() -> None:
    rng = np.random.default_rng(0)
    x = rng.choice([-1.0, 0.0, 1.0], size=(4000, 5), p=[0.2, 0.6, 0.2]).astype(np.float64)
    strengths = np.array([1.5, -1.0, 0.0, 0.5, -0.5])
    y = (rng.random(4000) < 1 / (1 + np.exp(-x @ strengths))).astype(float)

    w = teams._fit(x, y)

    p = 1 / (1 + np.exp(-x @ w))
    gradient = x.T @ (p - y) + teams.PENALTY * w
    assert np.abs(gradient).max() < 1e-6  # Newton's method has converged on the penalised optimum
    assert np.abs(w - strengths).max() < 0.15
