"""Players: each one picks among the options the referee offers, from what its side can see."""

import random

from trainerlab import _engine


class RandomBot:
    """Picks uniformly among the legal options: the baseline every other bot has to beat."""

    def __init__(self, *, seed: int) -> None:
        self._rng = random.Random(seed)

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        return self._rng.choice(legal)

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        return self._rng.choice(options)
