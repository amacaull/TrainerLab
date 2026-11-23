"""Measures one team builder against another: the same bot plays both sides.

A pair draws a core for each side from its seed; builder A completes side 0's core and builder B
side 1's, then they swap: each builder works on both cores, under the same dice. The bot that
plays changes nothing between the sides, so the score is the builders' alone.
"""

from collections.abc import Callable
from functools import cache, partial
from pathlib import Path

import numpy as np

from trainerlab.bots.roster import factory
from trainerlab.experiments import parallel
from trainerlab.experiments.teams import TEAM_SIZE, analyze
from trainerlab.experiments.tournament import TournamentResult
from trainerlab.game.battle import play_battle
from trainerlab.game.building import Builder, SpeciesBuilder, cores, random_builder
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.pairs import outcome_of


@cache
def species_scores(path: Path) -> dict[str, float]:
    """Each species' score in a `trainerlab teams` measure: the share of points of the teams
    that held it."""
    saved = np.load(path)
    size = int(saved["size"]) if "size" in saved else TEAM_SIZE
    return {s.name: s.score for s in analyze(saved["seed"], saved["points"], size).species}


def builder_of(spec: str) -> Builder:
    """A builder by its name: random, or species:FILE.npz (the scores of `trainerlab teams`)."""
    if spec == "random":
        return random_builder
    if spec.startswith("species:"):
        return SpeciesBuilder(species_scores(Path(spec.removeprefix("species:"))))
    raise ValueError(f"unknown builder {spec!r}: random, or species:FILE.npz")


def play_block(
    builder_a: str, builder_b: str, seeds: range, *, bot: str, size: int, core: list[str] | None
) -> TournamentResult:
    """Builder A's record over these pairs; `core`, if given, is both sides' core."""
    build_a, build_b, make = builder_of(builder_a), builder_of(builder_b), factory(bot)
    tally = {"win": 0, "draw": 0, "loss": 0}
    for seed in seeds:
        core0, core1 = (core, core) if core is not None else cores(seed, size)
        for a_side in (0, 1):
            first = build_a if a_side == 0 else build_b
            second = build_b if a_side == 0 else build_a
            team0, team1 = first(core0, size, 2 * seed), second(core1, size, 2 * seed + 1)
            env = BattleEnvironment(team0, team1, seed=seed)
            tally[outcome_of(play_battle(env, make(2 * seed), make(2 * seed + 1)), a_side)] += 1
    return TournamentResult(tally["win"], tally["draw"], tally["loss"])


def duel(
    builder_a: str,
    builder_b: str,
    seeds: range,
    jobs: int,
    *,
    bot: str,
    size: int,
    core: list[str] | None = None,
    progress: parallel.Meter | None = None,
    on_part: Callable[[TournamentResult], None] | None = None,
) -> TournamentResult:
    """Builder A's record against builder B over the pairs of these seeds, on `jobs` processes."""
    work = partial(play_block, bot=bot, size=size, core=core)
    parts = parallel.run(work, builder_a, builder_b, seeds, jobs, progress, on_part)
    return TournamentResult(
        wins=sum(p.wins for p in parts),
        draws=sum(p.draws for p in parts),
        losses=sum(p.losses for p in parts),
    )
