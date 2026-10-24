"""Measures one bot against another over paired battles, with a confidence interval.

A pair is two battles with the same seed and the same two teams, the bots swapping sides: each
bot plays both teams and both sides under the same luck, which cancels most of the variance.
A draw counts as half a win.
"""

import math
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from typing import Literal

from trainerlab.battle import play_battle
from trainerlab.bots import Bot
from trainerlab.environment import BattleEnvironment, random_team_pair

Outcome = Literal["win", "draw", "loss"]

# Builds a fresh bot from a seed, so that every battle starts from a known state.
BotFactory = Callable[[int], Bot]

# Gives the two teams of the pair played with a seed.
TeamSource = Callable[[int], tuple[list[str], list[str]]]

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


def pair_battles(
    make_a: BotFactory,
    make_b: BotFactory,
    *,
    seed: int,
    team0: list[str],
    team1: list[str],
) -> Iterator[tuple[int, BattleEnvironment, Bot, Bot]]:
    """The two battles of a pair, ready to play: bot A's side, the referee, the bots of sides 0, 1.

    The one place that decides how a seed sets up a battle, so that anything replaying a
    tournament plays exactly the battles it measured.
    """
    for a_side in (0, 1):
        env = BattleEnvironment(team0, team1, seed=seed)
        bot_a, bot_b = make_a(2 * seed), make_b(2 * seed + 1)
        bot0, bot1 = (bot_a, bot_b) if a_side == 0 else (bot_b, bot_a)
        yield a_side, env, bot0, bot1


def play_pair(
    make_a: BotFactory,
    make_b: BotFactory,
    *,
    seed: int,
    team0: list[str],
    team1: list[str],
) -> tuple[Outcome, Outcome]:
    """Plays one pair of battles and returns bot A's outcome in each."""
    outcomes = [
        outcome_of(play_battle(env, bot0, bot1), a_side)
        for a_side, env, bot0, bot1 in pair_battles(
            make_a, make_b, seed=seed, team0=team0, team1=team1
        )
    ]
    return outcomes[0], outcomes[1]


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


def outcome_of(winner: int | None, side: int) -> Outcome:
    """How a battle ended for `side`, given the winning side (None for a draw)."""
    if winner is None:
        return "draw"
    return "win" if winner == side else "loss"
