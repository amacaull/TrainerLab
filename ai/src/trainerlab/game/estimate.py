"""The bots' mental model of damage: a rough estimate from what a player can see.

This is a belief, not the rules: the engine alone applies real damage. Sets are fixed and public,
and stats, stat stages, held items and remaining HP are on screen, so every input is visible. Like
a player reckoning in their head, the estimate keeps the big factors (stats, power, STAB, types,
the common damage and speed items) and leaves out abilities, weather, critical hits and the
damage roll, for which it takes the average.
"""

from trainerlab import _engine

LEVEL_FACTOR = 2 * 100 / 5 + 2  # every Pokemon is level 100
AVERAGE_ROLL = 0.925  # damage rolls are uniform between 85 % and 100 %
STAB_BONUS = 1.5
PHYSICAL = 0
ATTACK, DEFENSE, SPECIAL_ATTACK, SPECIAL_DEFENSE, SPEED = range(5)
PARALYSIS = 4
# Held items a player factors in: damage multipliers by category, and the speed one.
_POWER_ITEMS = {"ChoiceBand": (1.5, 1.0), "ChoiceSpecs": (1.0, 1.5), "LifeOrb": (1.3, 1.3)}
_SPEED_ITEM, _SPEED_ITEM_BOOST = "ChoiceScarf", 1.5


def estimate_damage(
    attacker: _engine.BattlePokemon, defender: _engine.BattlePokemon, slot: int
) -> float:
    """The HP that the move in `slot` should take from `defender`, if it hits."""
    move = _engine.move_entry(attacker.move_ids[slot])
    if move.power == 0:
        return 0.0
    if move.category == PHYSICAL:
        attack = _staged(attacker.stats.attack, attacker, ATTACK)
        defense = _staged(defender.stats.defense, defender, DEFENSE)
    else:
        attack = _staged(attacker.stats.special_attack, attacker, SPECIAL_ATTACK)
        defense = _staged(defender.stats.special_defense, defender, SPECIAL_DEFENSE)
    base = LEVEL_FACTOR * move.power * attack / defense / 50 + 2
    types = _engine.species_entry(attacker.species_id)
    stab = STAB_BONUS if move.type in (types.type1, types.type2) else 1.0
    effectiveness = _engine.type_effectiveness(move.type, defender.species_id)
    physical_boost, special_boost = _POWER_ITEMS.get(_held_item(attacker), (1.0, 1.0))
    item = physical_boost if move.category == PHYSICAL else special_boost
    return base * stab * effectiveness * item * AVERAGE_ROLL


def strongest_hit(attacker: _engine.BattlePokemon, defender: _engine.BattlePokemon) -> float:
    """The largest damage estimate among the attacker's four moves."""
    return max(estimate_damage(attacker, defender, slot) for slot in range(len(attacker.move_ids)))


def effective_speed(pokemon: _engine.BattlePokemon) -> float:
    speed = _staged(pokemon.stats.speed, pokemon, SPEED)
    if _held_item(pokemon) == _SPEED_ITEM:
        speed *= _SPEED_ITEM_BOOST
    return speed / 2 if pokemon.status == PARALYSIS else speed


def _held_item(pokemon: _engine.BattlePokemon) -> str:
    if pokemon.item_id == _engine.NO_ITEM or pokemon.item_consumed:
        return ""
    return _engine.item_name(pokemon.item_id)


def _staged(value: int, pokemon: _engine.BattlePokemon, stat: int) -> float:
    stage = pokemon.stat_stages[stat]
    return value * (2 + stage) / 2 if stage >= 0 else value * 2 / (2 - stage)
