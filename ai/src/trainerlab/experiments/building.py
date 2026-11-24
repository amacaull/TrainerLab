"""Measures one team builder against another: the same bot plays both sides.

A pair draws from its seed a core for each side, then a pool of species outside them, the same for
both sides (game/building.py); builder A completes side 0's core from the pool and builder B
side 1's, then they swap: each builder works on both cores, under the same dice. The bot that
plays changes nothing between the sides, so the score is the builders' alone.
"""

from collections.abc import Callable
from functools import cache, partial
from pathlib import Path

import numpy as np

from trainerlab.bots.roster import factory
from trainerlab.experiments import arena, parallel
from trainerlab.experiments.teams import TEAM_SIZE, analyze
from trainerlab.experiments.tournament import TournamentResult
from trainerlab.game.battle import play_battle
from trainerlab.game.building import Builder, SpeciesBuilder, cores, draw_pool, random_builder
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


def battles(
    builder_a: str, builder_b: str, seed: int, *, size: int, core: list[str] | None, pool: int
) -> list[arena.Battle]:
    """The pair's two battles, under the same dice: builder A on side 0, then on side 1; each
    side keeps its core, the pool is both sides'."""
    build_a, build_b = builder_of(builder_a), builder_of(builder_b)
    core0, core1 = (core, core) if core is not None else cores(seed, size)
    picks = draw_pool(seed, pool, size, (core0, core1))
    return [
        (build_a(core0, size, 2 * seed, picks), build_b(core1, size, 2 * seed + 1, picks), seed),
        (build_b(core0, size, 2 * seed, picks), build_a(core1, size, 2 * seed + 1, picks), seed),
    ]


def play_block(
    builder_a: str,
    builder_b: str,
    seeds: range,
    *,
    bot: str,
    size: int,
    core: list[str] | None,
    pool: int,
) -> TournamentResult:
    """Builder A's record over these pairs, in pools of `pool` species (all of them: free
    choice); `core`, if given, is both sides' core."""
    make = factory(bot)
    tally = {"win": 0, "draw": 0, "loss": 0}
    for seed in seeds:
        pair = battles(builder_a, builder_b, seed, size=size, core=core, pool=pool)
        for a_side, (team0, team1, number) in enumerate(pair):
            env = BattleEnvironment(team0, team1, seed=number)
            winner = play_battle(env, make(2 * number), make(2 * number + 1))
            tally[outcome_of(winner, a_side)] += 1
    return TournamentResult(tally["win"], tally["draw"], tally["loss"])


def duel(
    builder_a: str,
    builder_b: str,
    seeds: range,
    jobs: int,
    *,
    bot: str,
    size: int,
    pool: int,
    core: list[str] | None = None,
    progress: parallel.Meter | None = None,
    on_part: Callable[[TournamentResult], None] | None = None,
) -> TournamentResult:
    """Builder A's record against builder B over the pairs of these seeds, on `jobs` processes;
    a search bot plays in C++, battles side by side (experiments/arena.py), on `jobs` threads."""
    found = arena.searcher_of(bot)
    if found is not None:
        listed = [
            battle
            for seed in seeds
            for battle in battles(builder_a, builder_b, seed, size=size, core=core, pool=pool)
        ]
        done = None if progress is None else (lambda count: progress.update(count // 2))
        tally = {"win": 0, "draw": 0, "loss": 0}
        for k, winner in enumerate(arena.play(found, listed, jobs, done)):
            tally[outcome_of(winner, k % 2)] += 1
        if progress is not None:
            progress.finish()
        result = TournamentResult(tally["win"], tally["draw"], tally["loss"])
        if on_part is not None:
            on_part(result)
        return result
    work = partial(play_block, bot=bot, size=size, core=core, pool=pool)
    parts = parallel.run(work, builder_a, builder_b, seeds, jobs, progress, on_part)
    return TournamentResult(
        wins=sum(p.wins for p in parts),
        draws=sum(p.draws for p in parts),
        losses=sum(p.losses for p in parts),
    )
