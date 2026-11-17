"""Measures one bot against another over paired battles, with a confidence interval.

A draw counts as half a win. The pairs can be spread over the cores: each one depends on its seed
alone, so the record is the same whatever the number of processes.
"""

import math
from dataclasses import dataclass

from trainerlab.bots.roster import factory
from trainerlab.experiments import parallel
from trainerlab.game.environment import random_team_pair
from trainerlab.game.pairs import BotFactory, TeamSource, play_pair

Z_95 = 1.96  # two-sided 95 % confidence


def wilson_interval(score: float, games: int) -> tuple[float, float]:
    """The 95 % Wilson score interval of a success rate."""
    if games <= 0:
        raise ValueError("A confidence interval needs at least one game")
    rate = score / games
    spread = Z_95 * Z_95 / games
    center = (rate + spread / 2) / (1 + spread)
    half_width = Z_95 * math.sqrt(rate * (1 - rate) / games + spread / (4 * games)) / (1 + spread)
    # Rounding can push a bound a hair outside [0, 1] when the rate is exactly 0 or 1.
    return max(0.0, center - half_width), min(1.0, center + half_width)


@dataclass(frozen=True)
class TournamentResult:
    """Bot A's record against bot B."""

    wins: int
    draws: int
    losses: int

    @property
    def games(self) -> int:
        return self.wins + self.draws + self.losses

    @property
    def score(self) -> float:
        return (self.wins + self.draws / 2) / self.games

    @property
    def interval(self) -> tuple[float, float]:
        return wilson_interval(self.wins + self.draws / 2, self.games)


def run_tournament(
    make_a: BotFactory,
    make_b: BotFactory,
    *,
    pairs: int = 500,
    first_seed: int = 0,
    teams: TeamSource = random_team_pair,
) -> TournamentResult:
    """Plays `pairs` pairs of battles, from seed `first_seed` upwards, and tallies A's record.

    By default each pair draws its own two random teams, so that a result holds across team
    compositions rather than for one matchup.
    """
    tally = {"win": 0, "draw": 0, "loss": 0}
    for seed in range(first_seed, first_seed + pairs):
        team0, team1 = teams(seed)
        for outcome in play_pair(make_a, make_b, seed=seed, team0=team0, team1=team1):
            tally[outcome] += 1
    return TournamentResult(wins=tally["win"], draws=tally["draw"], losses=tally["loss"])


def play_block(bot_a: str, bot_b: str, seeds: range) -> TournamentResult:
    """One block of pairs, by the bots' names: a process of its own when spread over the cores."""
    return run_tournament(factory(bot_a), factory(bot_b), pairs=len(seeds), first_seed=seeds[0])


def duel(
    bot_a: str,
    bot_b: str,
    seeds: range,
    jobs: int,
    progress: parallel.Progress | None = None,
) -> TournamentResult:
    """Bot A's record against bot B over the pairs of these seeds, on `jobs` processes."""
    parts = parallel.run(play_block, bot_a, bot_b, seeds, jobs, progress)
    return TournamentResult(
        wins=sum(p.wins for p in parts),
        draws=sum(p.draws for p in parts),
        losses=sum(p.losses for p in parts),
    )


def losses(bot_a: str, bot_b: str, seeds: range) -> list[tuple[int, int]]:
    """The battles bot A lost over these seeds: (seed, bot A's side), in order."""
    lost = []
    for seed in seeds:
        team0, team1 = random_team_pair(seed)
        outcomes = play_pair(factory(bot_a), factory(bot_b), seed=seed, team0=team0, team1=team1)
        lost += [(seed, a_side) for a_side, outcome in enumerate(outcomes) if outcome == "loss"]
    return lost
