"""Measures one bot against another over paired battles, with a confidence interval.

A draw counts as half a win. The pairs can be spread over the cores: each one depends on its seed
alone, so the record is the same whatever the number of processes.
"""

import math
from collections.abc import Callable
from dataclasses import dataclass
from functools import partial

from trainerlab.bots.roster import factory
from trainerlab.experiments import parallel
from trainerlab.game.battle import play_battle
from trainerlab.game.building import species_team_pair
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.game.pairs import BotFactory, TeamSource, outcome_of, pair_battles, play_pair

Z_95 = 1.96  # two-sided 95 % confidence
# The teams a duel is played on: random ones, or both built by the species strengths, as the bot
# of `trainerlab play` builds its own (palier 13.2).
TEAMS: dict[str, TeamSource] = {"random": random_team_pair, "species": species_team_pair}


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


def play_block(bot_a: str, bot_b: str, seeds: range, teams: str = "random") -> TournamentResult:
    """One block of pairs, by the bots' names: a process of its own when spread over the cores."""
    return run_tournament(
        factory(bot_a), factory(bot_b), pairs=len(seeds), first_seed=seeds[0], teams=TEAMS[teams]
    )


def duel(
    bot_a: str,
    bot_b: str,
    seeds: range,
    jobs: int,
    progress: parallel.Meter | None = None,
    on_part: Callable[[TournamentResult], None] | None = None,
    teams: str = "random",
) -> TournamentResult:
    """Bot A's record against bot B over the pairs of these seeds, on `jobs` processes, on
    `teams` (TEAMS); `on_part` hears the record of each block of pairs as it ends."""
    work = partial(play_block, teams=teams)
    parts = parallel.run(work, bot_a, bot_b, seeds, jobs, progress, on_part)
    return TournamentResult(
        wins=sum(p.wins for p in parts),
        draws=sum(p.draws for p in parts),
        losses=sum(p.losses for p in parts),
    )


@dataclass(frozen=True)
class Loss:
    """A battle bot A lost: its pair's seed, bot A's side, its length, the Pokemon left."""

    seed: int
    side: int
    turns: int
    mine: int
    theirs: int


def losses(
    bot_a: str, bot_b: str, seeds: range, progress: Callable[[int], None] | None = None
) -> list[Loss]:
    """The battles bot A lost over these seeds, in order; `progress` hears the pairs done."""
    lost = []
    for done, seed in enumerate(seeds, start=1):
        team0, team1 = random_team_pair(seed)
        battles = pair_battles(factory(bot_a), factory(bot_b), seed=seed, team0=team0, team1=team1)
        for a_side, env, bot0, bot1 in battles:
            if outcome_of(play_battle(env, bot0, bot1), a_side) == "loss":
                left = [_standing(env, side) for side in (a_side, 1 - a_side)]
                lost.append(Loss(seed, a_side, env.turn, left[0], left[1]))
        if progress is not None:
            progress(done)
    return lost


def _standing(env: BattleEnvironment, side: int) -> int:
    view = env.observe(side)
    return sum(1 for i in range(view.team_size[side]) if not view.pokemon(side, i).is_fainted())
