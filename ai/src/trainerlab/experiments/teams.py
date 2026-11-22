"""Does the composition of a team decide battles? One bot against itself, over paired battles.

A pair here plays the same two teams twice, with the same bot on both sides, but with dice of
their own and the teams on swapped sides: two independent replays of one matchup. (A tournament's
pair shares its dice, and a bot that does not draw would replay the same battle.) If teams did not
matter, the team that wins the first battle would win the second only half the time, pairs with a
draw aside: the share of pairs won twice by the same team measures how much the teams decide,
without any model.

Two finer measures follow:

- each species' score: the share of points won by the teams that hold it (a draw is half a point),
  0.5 if it made no difference;
- how well the composition alone calls the winner: a logistic regression on which species each
  side holds, fitted on the pairs whose seed is not a multiple of 5 and scored on the others.
"""

import math
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from trainerlab import _engine
from trainerlab.bots.roster import factory
from trainerlab.experiments import parallel
from trainerlab.experiments.tournament import wilson_interval
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import DATA_DIR, BattleEnvironment, random_team_pair
from trainerlab.game.pairs import outcome_of

TEST_EVERY = 5  # the pairs held out to score the regression, as for the win model
PENALTY = 10.0  # L2 on the regression's weights: 49 of them, a few thousand battles
POINTS = {"win": 1.0, "draw": 0.5, "loss": 0.0}

Floats = npt.NDArray[np.float64]


def play_block(bot: str, _: str, seeds: range) -> list[float]:
    """The first team's points in both battles of each pair: a process of its own on a core."""
    make = factory(bot)
    points: list[float] = []
    for seed in seeds:
        team0, team1 = random_team_pair(seed)
        for replay in (0, 1):
            number = 2 * seed + replay  # dice and bots of its own
            first, second = (team0, team1) if replay == 0 else (team1, team0)
            env = BattleEnvironment(first, second, seed=number)
            winner = play_battle(env, make(2 * number), make(2 * number + 1))
            side = 0 if replay == 0 else 1  # where the first team plays
            points.append(POINTS[outcome_of(winner, side)])
    return points


def record(
    bot: str, seeds: range, jobs: int, progress: parallel.Meter | None = None
) -> tuple[npt.NDArray[np.int64], Floats]:
    """The seed and the first team's points of every battle, two per pair, in seed order."""
    parts = parallel.run(play_block, bot, bot, seeds, jobs, progress)
    points = np.array([p for part in parts for p in part], dtype=np.float64)
    return np.repeat(np.arange(seeds.start, seeds.stop, dtype=np.int64), 2), points


@dataclass(frozen=True)
class Species:
    name: str
    score: float  # share of points won by the teams holding it
    low: float
    high: float
    battles: int


@dataclass(frozen=True)
class Analysis:
    battles: int
    agreement: float  # share of draw-free pairs won twice by the same team
    agreement_low: float
    agreement_high: float
    decided_pairs: int
    accuracy: float  # held-out battles (draws aside) whose winner the regression calls right
    log_loss: float  # held-out, against log(2) = 0.693 for a coin
    species: list[Species]  # best first


def analyze(seeds: npt.NDArray[np.int64], points: Floats) -> Analysis:
    """What these battles say about the teams; `seeds` and `points` as `record` gives them."""
    first, second = points[0::2], points[1::2]
    decided = (first != 0.5) & (second != 0.5)
    same = int((first[decided] == second[decided]).sum())
    count = int(decided.sum())
    low, high = wilson_interval(same, count) if count else (math.nan, math.nan)

    names, x = _compositions(seeds)
    test = seeds % TEST_EVERY == 0
    weights = _fit(x[~test], points[~test])
    p = 1 / (1 + np.exp(-x[test] @ weights))
    y = points[test]
    eps = 1e-12
    log_loss = float(-(y * np.log(p + eps) + (1 - y) * np.log(1 - p + eps)).mean())
    called = y != 0.5
    accuracy = float(((p[called] > 0.5) == (y[called] == 1.0)).mean()) if called.any() else 0.0

    species = []
    for k, name in enumerate(names):
        mine, theirs = x[:, k] == 1, x[:, k] == -1
        won = points[mine].sum() + (1 - points[theirs]).sum()
        n = int(mine.sum() + theirs.sum())
        s_low, s_high = wilson_interval(float(won), n) if n else (math.nan, math.nan)
        species.append(Species(name, float(won) / n if n else math.nan, s_low, s_high, n))
    species.sort(key=lambda s: -s.score)
    return Analysis(
        len(points),
        same / count if count else math.nan,
        low,
        high,
        count,
        accuracy,
        log_loss,
        species,
    )


def _compositions(seeds: npt.NDArray[np.int64]) -> tuple[list[str], Floats]:
    """The species, and per battle +1 for the first team's species, -1 for the second's."""
    _engine.engine_init(str(DATA_DIR))
    names = [_engine.species_id_string(i) for i in range(_engine.species_count())]
    index = {name: k for k, name in enumerate(names)}
    x = np.zeros((len(seeds), len(names)))
    for row, seed in enumerate(seeds):
        team0, team1 = random_team_pair(int(seed))
        for name in team0:
            x[row, index[name]] += 1
        for name in team1:
            x[row, index[name]] -= 1
    return names, x


def _fit(x: Floats, y: Floats, steps: int = 25) -> Floats:
    """Logistic regression without intercept (the teams swap sides), by Newton's method."""
    w = np.zeros(x.shape[1])
    for _ in range(steps):
        p = 1 / (1 + np.exp(-x @ w))
        gradient = x.T @ (p - y) + PENALTY * w
        hessian = (x.T * (p * (1 - p))) @ x + PENALTY * np.eye(len(w))
        w -= np.linalg.solve(hessian, gradient)
    return w
