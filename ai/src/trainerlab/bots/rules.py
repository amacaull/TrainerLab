"""Rules 1 to 5: bots made of three choices, each written by hand.

A rule bot is the assembly of a move choice, a replacement choice, and a switch it may make before
moving. Each rule below is one line that names its three choices; each choice is a function of
what the side sees and of the options the referee offers.
"""

from collections.abc import Callable
from dataclasses import dataclass

from trainerlab import _engine
from trainerlab.game import estimate

Action = _engine.Action
View = _engine.BattleState
MoveChoice = Callable[[int, View, list[Action]], Action]  # among the moves offered
Replacement = Callable[[int, View, list[int]], int]  # among the team indices offered
Switch = Callable[[int, View, list[Action]], Action | None]  # None: stay and move


@dataclass(frozen=True)
class RuleBot:
    move: MoveChoice
    replacement: Replacement
    switch: Switch | None = None

    def choose_action(self, side: int, view: View, legal: list[Action]) -> Action:
        if self.switch is not None:
            leaving = self.switch(side, view, legal)
            if leaving is not None:
                return leaving
        moves = [action for action in legal if action.kind == _engine.ACTION_MOVE]
        return self.move(side, view, moves)

    def choose_replacement(self, side: int, view: View, options: list[int]) -> int:
        return self.replacement(side, view, options)


# ---------------------------------------------------------------------------------------------
# The rules


def hitter() -> RuleBot:
    """Rule 1: plays the move with the highest power x STAB x type effectiveness x accuracy, never
    switches while it can move, and sends in the first Pokemon offered."""
    return RuleBot(move=most_power, replacement=first_offered)


def dodger() -> RuleBot:
    """Rule 2: rule 1, but leaves a super effective threat for a Pokemon that resists it.

    A measured negative result: it loses to rule 1 on random teams."""
    return RuleBot(move=most_power, replacement=first_offered, switch=refuge)


def matcher() -> RuleBot:
    """Rule 3: rule 1, but sends in the bench Pokemon with the best matchup after a knock-out.

    Built on rule 1, not rule 2: rule 2 loses to rule 1."""
    return RuleBot(move=most_power, replacement=best_matchup)


def survivor() -> RuleBot:
    """Rule 4: rule 3, but leaves a healthy Pokemon the foe would knock out before it acts."""
    return RuleBot(move=most_power, replacement=best_matchup, switch=escape)


def calculator() -> RuleBot:
    """Rule 5: rule 4, but plays the move with the most damage the engine itself expects."""
    return RuleBot(move=most_expected_damage, replacement=best_matchup, switch=escape)


# ---------------------------------------------------------------------------------------------
# Move choices

STAB_BONUS = 1.5  # same-type attack bonus


def most_power(side: int, view: View, moves: list[Action]) -> Action:
    """The move with the highest power x STAB x type effectiveness x accuracy; status moves 0."""
    attacker, defender = view.active(side), view.active(1 - side).species_id
    return max(moves, key=lambda move: _power_score(attacker, move.index, defender))


def most_expected_damage(side: int, view: View, moves: list[Action]) -> Action:
    """The move the engine expects to take the most HP from the foe (`expected_damage`, its mean).

    The engine plays the move through its own rules, so it counts what the power score leaves
    out: abilities, items, stats, fixed damage, every hit of a multi-hit move, moves that fail."""
    return max(moves, key=lambda move: _engine.expected_damage(view, side, move.index).mean)


def _power_score(attacker: _engine.BattlePokemon, slot: int, defender_species: int) -> float:
    move = _engine.move_entry(attacker.move_ids[slot])
    attacker_types = _engine.species_entry(attacker.species_id)
    stab = STAB_BONUS if move.type in (attacker_types.type1, attacker_types.type2) else 1.0
    effectiveness = _engine.type_effectiveness(move.type, defender_species)
    hit_chance = move.accuracy / 100 if move.accuracy > 0 else 1.0
    return move.power * stab * effectiveness * hit_chance


def _strongest_slot(attacker: _engine.BattlePokemon, defender_species: int) -> int:
    slots = range(len(attacker.move_ids))
    return max(slots, key=lambda slot: _power_score(attacker, slot, defender_species))


def _best_power(attacker: _engine.BattlePokemon, defender_species: int) -> float:
    return _power_score(attacker, _strongest_slot(attacker, defender_species), defender_species)


# ---------------------------------------------------------------------------------------------
# Replacement choices


def first_offered(side: int, view: View, options: list[int]) -> int:
    return options[0]


def best_matchup(side: int, view: View, options: list[int]) -> int:
    """The bench Pokemon with the best matchup against the foe's active: the most it can deal,
    minus the most it can take."""
    foe = view.active(1 - side)
    return max(options, key=lambda index: _matchup(view.pokemon(side, index), foe))


def _matchup(candidate: _engine.BattlePokemon, foe: _engine.BattlePokemon) -> float:
    return _best_power(candidate, foe.species_id) - _best_power(foe, candidate.species_id)


# ---------------------------------------------------------------------------------------------
# Switches made before moving

WORTH_SAVING = 0.5  # share of its HP a threatened Pokemon must keep to be worth saving
CHEAP_HIT = 1 / 3  # share of its remaining HP the incoming Pokemon may lose to the hit


def refuge(side: int, view: View, legal: list[Action]) -> Action | None:
    """A switch to a Pokemon that resists the foe's strongest move, when it is super effective."""
    foe, active = view.active(1 - side), view.active(side)
    best = _strongest_slot(foe, active.species_id)
    if _power_score(foe, best, active.species_id) == 0:
        return None
    threat = _engine.move_entry(foe.move_ids[best]).type
    if _engine.type_effectiveness(threat, active.species_id) < 2:
        return None
    resisting = [
        action
        for action in legal
        if action.kind == _engine.ACTION_SWITCH
        and _engine.type_effectiveness(threat, view.pokemon(side, action.index).species_id) <= 0.5
    ]
    return resisting[0] if resisting else None


def escape(side: int, view: View, legal: list[Action]) -> Action | None:
    """A switch away from a Pokemon the foe would knock out before it acts, if it still has half
    its HP and a bench Pokemon loses at most a third of its HP to the hit: the best matchup among
    those. Damage and speed come from the mental model in `estimate`, not from the engine."""
    active, foe = view.active(side), view.active(1 - side)
    if active.current_hp < WORTH_SAVING * active.stats.hp:
        return None  # a weakened Pokemon is worth more as a free switch-in when it faints
    outsped = estimate.effective_speed(foe) > estimate.effective_speed(active)
    if not outsped or estimate.strongest_hit(foe, active) < active.current_hp:
        return None
    survivors = [
        action
        for action in legal
        if action.kind == _engine.ACTION_SWITCH
        and estimate.strongest_hit(foe, view.pokemon(side, action.index))
        <= CHEAP_HIT * view.pokemon(side, action.index).current_hp
    ]
    if not survivors:
        return None
    return max(survivors, key=lambda switch: _matchup(view.pokemon(side, switch.index), foe))
