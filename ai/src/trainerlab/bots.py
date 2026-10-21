"""Players: each one picks among the options the referee offers, from what its side can see."""

import random
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


STAB_BONUS = 1.5  # same-type attack bonus


class HeuristicBot:
    """Rule 1: plays the move with the highest expected damage against the opposing active.

    A move scores power x STAB x type effectiveness x accuracy. Status moves score 0, switching is
    never chosen while a move is available, and the first proposed Pokemon replaces a fainted one.
    """

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        attacker = view.active(side)
        defender_species = view.active(1 - side).species_id
        moves = [action for action in legal if action.kind == _engine.ACTION_MOVE]
        return max(moves, key=lambda move: _expected_damage(attacker, move.index, defender_species))

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        return options[0]


def _expected_damage(attacker: _engine.BattlePokemon, slot: int, defender_species: int) -> float:
    move = _engine.move_entry(attacker.move_ids[slot])
    attacker_types = _engine.species_entry(attacker.species_id)
    stab = STAB_BONUS if move.type in (attacker_types.type1, attacker_types.type2) else 1.0
    effectiveness = _engine.type_effectiveness(move.type, defender_species)
    hit_chance = move.accuracy / 100 if move.accuracy > 0 else 1.0
    return move.power * stab * effectiveness * hit_chance
