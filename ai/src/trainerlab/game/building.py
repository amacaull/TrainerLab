"""Teams built around a core: the seed imposes one or two Pokemon, a builder adds the rest.

In a draft, the seed also draws a pool of species, the same for both sides, outside the cores, and
without Megas or legendaries if a core holds one; the builder completes its core from the pool
alone. With every species in the pool, the choice is free.

A builder takes the core, the size of the team, a seed (for those that draw) and the pool, and
returns the whole team, the core first. Every species has one set (moves, item, ability), so a
team is its species alone.
"""

import math
import random
from collections.abc import Callable, Sequence
from itertools import combinations

from trainerlab import _engine
from trainerlab.game.environment import DATA_DIR, _build_team

# A team from its core, its size, a seed and the pool it picks from.
Builder = Callable[[list[str], int, int, list[str]], list[str]]


def species() -> list[str]:
    """Every species of the engine, in its order."""
    _engine.engine_init(str(DATA_DIR))
    return [_engine.species_id_string(i) for i in range(_engine.species_count())]


def legal(team: list[str]) -> bool:
    """Whether the engine accepts the team (one Mega, one legendary at most...)."""
    try:
        _engine.validate_team(_build_team(team))
    except _engine.TeamError:
        return False
    return True


def completable(core: list[str], pool: Sequence[str], size: int) -> bool:
    """Whether some species of the pool complete the core into a legal team."""
    others = [name for name in pool if name not in core]
    return any(legal([*core, *more]) for more in combinations(others, size - len(core)))


def cores(seed: int, size: int) -> tuple[list[str], list[str]]:
    """The core of each side, the same for the same seed: one or two Pokemon, half the time each,
    always fewer than the team holds, so that something is left to choose."""
    rng = random.Random(f"core-{seed}")
    every = species()

    def core() -> list[str]:
        count = rng.choice((1, 2)) if size > 2 else 1
        while True:
            drawn = rng.sample(every, count)
            if legal(drawn):
                return drawn

    return core(), core()


def draw_pool(seed: int, count: int, size: int, both: Sequence[list[str]]) -> list[str]:
    """The species a draft picks from, the same for both sides and for the same seed: none of
    the cores, no Mega if a core holds one, no legendary if a core holds one, and drawn again
    until every core can be completed into a legal team. Every species when `count` reaches
    them all: free choice."""
    every = species()
    if count >= len(every):
        return every
    taken = {name for core in both for name in core}
    entries = {name: _engine.species_entry(_engine.find_species_id(name)) for name in every}
    no_mega = any(entries[name].mega for name in taken)
    no_legendary = any(entries[name].legendary for name in taken)
    available = [
        name
        for name in every
        if name not in taken
        and not (no_mega and entries[name].mega)
        and not (no_legendary and entries[name].legendary)
    ]
    rng = random.Random(f"pool-{seed}")
    while True:
        pool = rng.sample(available, count)
        if all(completable(core, pool, size) for core in both):
            return pool


def random_builder(core: list[str], size: int, seed: int, pool: list[str]) -> list[str]:
    """The core, completed from the pool at random until the team is legal."""
    rng = random.Random(f"build-{seed}")
    others = [name for name in pool if name not in core]
    while True:
        team = core + rng.sample(others, size - len(core))
        if legal(team):
            return team


class SpeciesBuilder:
    """Adds the best scored species, one by one, skipping those after which the pool could no
    longer complete a legal team."""

    def __init__(self, scores: dict[str, float]) -> None:
        # A species never seen has no score (nan): last.
        self.ranked = sorted(
            scores, key=lambda name: math.inf if math.isnan(scores[name]) else -scores[name]
        )

    def __call__(self, core: list[str], size: int, seed: int, pool: list[str]) -> list[str]:
        team = list(core)
        for name in self.ranked:
            if name in pool and name not in team and completable([*team, name], pool, size):
                team.append(name)
                if len(team) == size:
                    return team
        raise ValueError(f"{len(self.ranked)} scored species cannot complete a team of {size}")
