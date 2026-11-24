"""The true score of teams: each plays pairs against random teams of its size.

With 49 species there are 1093 legal teams of two, few enough to measure them all; of larger
teams, a sample drawn once for all (palier 12.4). Each team plays `pairs` pairs, a random team of
its size drawn from each seed, the matchup twice with the teams on swapped sides and dice of their
own (as experiments/teams.py). The score of a team is the share of its points; it is the truth a
model of teams is checked against (palier 12.2).

The seeds run through the teams in order: team i plays seeds first + i * pairs, and so on.
"""

import math
import random
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


@cache
def _sample(size: int, count: int) -> list[list[str]]:
    rng = random.Random(f"truth-{size}")
    drawn: list[list[str]] = []
    seen: set[tuple[str, ...]] = set()
    while len(drawn) < count:
        team = rng.sample(species(), size)
        if legal(team) and tuple(sorted(team)) not in seen:
            seen.add(tuple(sorted(team)))
            drawn.append(team)
    return drawn


def teams_of(size: int, count: int | None) -> list[list[str]]:
    """The teams measured: of two, every one (the first `count`, to time a run); larger, a sample
    of `count`, the same for any count (the first of a larger sample)."""
    if size == SIZE:
        return every_team()[:count]
    if count is None:
        raise ValueError(f"teams of {size}: how many to sample?")
    return _sample(size, count)


def battles(
    seed: int, *, first: int, pairs: int, size: int = SIZE, count: int | None = None
) -> list[arena.Battle]:
    """The pair of this seed: its team against a random team of its size, once each way, with
    dice of its own (2 * seed and 2 * seed + 1); the team plays side 0, then side 1."""
    team = teams_of(size, count)[(seed - first) // pairs]
    foe = random_team_pair(seed, size)[0]
    return [(team, foe, 2 * seed), (foe, team, 2 * seed + 1)]


def play_block(
    bot: str, size: str, seeds: range, *, first: int, pairs: int, count: int | None
) -> list[float]:
    """The team's points in both battles of each pair: a process of its own on a core. The size
    comes as text, where a duel's block takes bot B's name."""
    make = factory(bot)
    points: list[float] = []
    for seed in seeds:
        pair = battles(seed, first=first, pairs=pairs, size=int(size), count=count)
        for side, (team0, team1, number) in enumerate(pair):
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
    size: int = SIZE,
    progress: parallel.Meter | None = None,
) -> tuple[npt.NDArray[np.int64], npt.NDArray[np.float64]]:
    """Each battle's team (its place in every_team()) and its points, the first `count` teams
    (all of them by default), in order. A search bot plays in C++, battles side by side
    (experiments/arena.py), on `jobs` threads."""
    teams = len(teams_of(size, count))
    seeds = range(first, first + teams * pairs)
    found = arena.searcher_of(bot)
    if found is None:
        work = partial(play_block, first=first, pairs=pairs, count=count)
        parts = parallel.run(work, bot, str(size), seeds, jobs, progress)
        played = [p for part in parts for p in part]
    else:
        listed = [
            b
            for seed in seeds
            for b in battles(seed, first=first, pairs=pairs, size=size, count=count)
        ]
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


def analyze(
    index: npt.NDArray[np.int64],
    points: npt.NDArray[np.float64],
    teams: list[list[str]] | None = None,
) -> Truth:
    """Each team's score and how much of their spread the dice explain; `teams` names them
    (every team of two by default)."""
    names = every_team() if teams is None else teams
    measured, scores, errors = [], [], []
    for team in np.unique(index):
        mine = points[index == team]
        score = float(mine.mean())
        low, high = wilson_interval(float(mine.sum()), len(mine))
        measured.append(Team(names[int(team)], score, low, high))
        scores.append(score)
        errors.append(score * (1 - score) / len(mine))
    spread = float(np.std(scores))
    noise = math.sqrt(float(np.mean(errors)))
    measured.sort(key=lambda t: -t.score)
    explained = 1 - noise**2 / spread**2 if spread > 0 else math.nan
    return Truth(measured, spread, noise, explained)
