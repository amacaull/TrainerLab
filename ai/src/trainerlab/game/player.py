"""The contract between the game loop and a player, bot or human."""

from typing import Protocol

from trainerlab import _engine


class Bot(Protocol):
    """What the game loop asks of a player: each call returns one element of the list it gets."""

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action: ...

    def choose_replacement(
        self, side: int, view: _engine.BattleState, options: list[int]
    ) -> int: ...
