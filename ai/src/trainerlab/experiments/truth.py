"""The true score of every team of two: each legal team plays pairs against random teams of two.

With 49 species there are 1093 legal teams of two, few enough to measure them all: each one
plays `pairs` pairs, a random team of two drawn from each seed, the matchup twice with the teams
on swapped sides and dice of their own (as experiments/teams.py). The score of a team is the share
of its points; it is the truth a model of teams is checked against (palier 12.2).

The seeds run through the teams in order: team i plays seeds first + i * pairs, and so on.
"""

import math
from dataclasses import dataclass
from functools import cache, partial
from itertools import combinations

import numpy as np
import numpy.typing as npt

from trainerlab.bots.roster import factory
from trainerlab.experiments import arena, parallel
from trainerlab.experiments.teams import POINTS
from trainerlab.experiments.tournament import wilson_interval
from trainerlab.game.battle import play_battle
from trainerlab.game.building import legal, species
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.game.pairs import outcome_of

SIZE = 2


@cache
def every_team() -> list[list[str]]:
    """Every legal team of two, in the engine's order of species."""
    return [list(team) for team in combinations(species(), SIZE) if legal(list(team))]


def battles(seed: int, *, first: int, pairs: int) -> list[arena.Battle]:
    """The pair of this seed: its team against a random team of two, once each way, with dice
    of its own (2 * seed and 2 * seed + 1); the team plays side 0, then side 1."""
    team = every_team()[(seed - first) // pairs]
    foe = random_team_pair(seed, SIZE)[0]
    return [(team, foe, 2 * seed), (foe, team, 2 * seed + 1)]


def play_block(bot: str, _: str, seeds: range, *, first: int, pairs: int) -> list[float]:
    """The team's points in both battles of each pair: a process of its own on a core."""
    make = factory(bot)
    points: list[float] = []
    for seed in seeds:
        for side, (team0, team1, number) in enumerate(battles(seed, first=first, pairs=pairs)):
            env = BattleEnvironment(team0, team1, seed=number)
            winner = play_battle(env, make(2 * number), make(2 * number + 1))
            points.append(POINTS[outcome_of(winner, side)])
    return points


def record(
    bot: str,
    *,
    first: int,
    pairs: int,
    jobs: int,
    count: int | None = None,
    progress: parallel.Meter | None = None,
) -> tuple[npt.NDArray[np.int64], npt.NDArray[np.float64]]:
    """Each battle's team (its place in every_team()) and its points, the first `count` teams
    (all of them by default), in order. A search bot plays in C++, battles side by side
    (experiments/arena.py), on `jobs` threads."""
    teams = len(every_team()) if count is None else count
    seeds = range(first, first + teams * pairs)
    found = arena.searcher_of(bot)
    if found is None:
        work = partial(play_block, first=first, pairs=pairs)
        parts = parallel.run(work, bot, "", seeds, jobs, progress)
        played = [p for part in parts for p in part]
    else:
        listed = [b for seed in seeds for b in battles(seed, first=first, pairs=pairs)]
        done = None if progress is None else (lambda count: progress.update(count // 2))
        winners = arena.play(found, listed, jobs, done)
        played = [POINTS[outcome_of(w, k % 2)] for k, w in enumerate(winners)]
        if progress is not None:
            progress.finish()
    points = np.array(played, dtype=np.float64)
    return np.repeat(np.arange(teams, dtype=np.int64), 2 * pairs), points


@dataclass(frozen=True)
class Team:
    names: list[str]
    score: float
    low: float
    high: float


@dataclass(frozen=True)
class Truth:
    teams: list[Team]  # best first
    spread: float  # standard deviation of the teams' scores
    noise: float  # typical error of one score: sqrt(mean s(1 - s) / n)
    explained: float  # share of the spread that is the teams', not the dice


def analyze(index: npt.NDArray[np.int64], points: npt.NDArray[np.float64]) -> Truth:
    """Each team's score and how much of their spread the dice explain."""
    teams, scores, errors = [], [], []
    for team in np.unique(index):
        mine = points[index == team]
        score = float(mine.mean())
        low, high = wilson_interval(float(mine.sum()), len(mine))
        teams.append(Team(every_team()[int(team)], score, low, high))
        scores.append(score)
        errors.append(score * (1 - score) / len(mine))
    spread = float(np.std(scores))
    noise = math.sqrt(float(np.mean(errors)))
    teams.sort(key=lambda t: -t.score)
    return Truth(teams, spread, noise, 1 - noise**2 / spread**2)
