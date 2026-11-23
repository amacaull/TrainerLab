"""Teams built around a core: the seed imposes one or two Pokemon, a builder adds the rest.

A builder takes the core, the size of the team and a seed (for those that draw), and returns the
whole team, the core first. Every species has one set (moves, item, ability), so a team is its
species alone.
"""

import random
from collections.abc import Callable

from trainerlab import _engine
from trainerlab.game.environment import DATA_DIR, _build_team

# A team from its core, its size and a seed.
Builder = Callable[[list[str], int, int], list[str]]


def _species() -> list[str]:
    _engine.engine_init(str(DATA_DIR))
    return [_engine.species_id_string(i) for i in range(_engine.species_count())]


def legal(team: list[str]) -> bool:
    """Whether the engine accepts the team (one Mega, one legendary at most...)."""
    try:
        _engine.validate_team(_build_team(team))
    except _engine.TeamError:
        return False
    return True


def cores(seed: int, size: int) -> tuple[list[str], list[str]]:
    """The core of each side, the same for the same seed: one or two Pokemon, half the time each,
    always fewer than the team holds, so that something is left to choose."""
    rng = random.Random(f"core-{seed}")
    species = _species()

    def core() -> list[str]:
        count = rng.choice((1, 2)) if size > 2 else 1
        while True:
            drawn = rng.sample(species, count)
            if legal(drawn):
                return drawn

    return core(), core()


def random_builder(core: list[str], size: int, seed: int) -> list[str]:
    """The core, completed at random until the team is legal."""
    rng = random.Random(f"build-{seed}")
    others = [name for name in _species() if name not in core]
    while True:
        team = core + rng.sample(others, size - len(core))
        if legal(team):
            return team


class SpeciesBuilder:
    """Adds the best scored species, one by one, skipping those that would break a rule."""

    def __init__(self, scores: dict[str, float]) -> None:
        self.ranked = sorted(scores, key=lambda name: -scores[name])

    def __call__(self, core: list[str], size: int, seed: int) -> list[str]:
        team = list(core)
        for name in self.ranked:
            if name not in team and legal([*team, name]):
                team.append(name)
                if len(team) == size:
                    return team
        raise ValueError(f"{len(self.ranked)} scored species cannot complete a team of {size}")
