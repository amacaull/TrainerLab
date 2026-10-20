"""Measures one bot against another over paired battles, with a confidence interval.

A pair is two battles with the same seed and the same two teams, the bots swapping sides: each
bot plays both teams and both sides under the same luck, which cancels most of the variance.
A draw counts as half a win.
"""

import math
from collections.abc import Callable
from dataclasses import dataclass
from typing import Literal

from trainerlab.battle import play_battle
from trainerlab.bots import Bot
from trainerlab.environment import BattleEnvironment

Outcome = Literal["win", "draw", "loss"]

# Builds a fresh bot from a seed, so that every battle starts from a known state.
BotFactory = Callable[[int], Bot]

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


def play_pair(
    make_a: BotFactory,
    make_b: BotFactory,
    *,
    seed: int,
    team0: list[str],
    team1: list[str],
) -> tuple[Outcome, Outcome]:
    """Plays one pair of battles and returns bot A's outcome in each."""
    outcomes: list[Outcome] = []
    for a_side in (0, 1):
        env = BattleEnvironment(team0, team1, seed=seed)
        bot_a, bot_b = make_a(2 * seed), make_b(2 * seed + 1)
        bots = (bot_a, bot_b) if a_side == 0 else (bot_b, bot_a)
        winner = play_battle(env, *bots)
        outcomes.append(_outcome(winner, a_side))
    return outcomes[0], outcomes[1]


def run_tournament(
    make_a: BotFactory,
    make_b: BotFactory,
    *,
    team0: list[str],
    team1: list[str],
    pairs: int = 500,
    first_seed: int = 0,
) -> TournamentResult:
    """Plays `pairs` pairs of battles, from seed `first_seed` upwards, and tallies A's record."""
    tally = {"win": 0, "draw": 0, "loss": 0}
    for seed in range(first_seed, first_seed + pairs):
        for outcome in play_pair(make_a, make_b, seed=seed, team0=team0, team1=team1):
            tally[outcome] += 1
    return TournamentResult(wins=tally["win"], draws=tally["draw"], losses=tally["loss"])


def _outcome(winner: int | None, a_side: int) -> Outcome:
    if winner is None:
        return "draw"
    return "win" if winner == a_side else "loss"
