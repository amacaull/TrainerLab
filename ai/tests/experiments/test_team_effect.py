import math
from collections.abc import Callable

import numpy as np

from trainerlab.experiments import teams
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
