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


class SwitchingBot(HeuristicBot):
    """Rule 2: rule 1, but leaves a Pokemon threatened by a super effective move for one that
    resists it, when the bench has one."""

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        refuge = _refuge(side, view, legal)
        return refuge if refuge is not None else super().choose_action(side, view, legal)


def _refuge(
    side: int, view: _engine.BattleState, legal: list[_engine.Action]
) -> _engine.Action | None:
    """The switch to make when the active faces a super effective threat, if any resists it."""
    opponent = view.active(1 - side)
    threat_type = _strongest_move_type(opponent, view.active(side).species_id)
    if threat_type is None:
        return None
    if _engine.type_effectiveness(threat_type, view.active(side).species_id) < 2:
        return None
    switches = [action for action in legal if action.kind == _engine.ACTION_SWITCH]
    resisting = [
        switch
        for switch in switches
        if _engine.type_effectiveness(threat_type, view.pokemon(side, switch.index).species_id)
        <= 0.5
    ]
    return resisting[0] if resisting else None


def _strongest_move_type(attacker: _engine.BattlePokemon, defender_species: int) -> int | None:
    """The type of the attacker's highest expected damage move, or None if none deals damage."""
    slots = range(len(attacker.move_ids))
    best = max(slots, key=lambda slot: _expected_damage(attacker, slot, defender_species))
    if _expected_damage(attacker, best, defender_species) == 0:
        return None
    return _engine.move_entry(attacker.move_ids[best]).type
