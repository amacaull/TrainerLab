"""Turns battle views and events into text for a human player.

Pure functions: nothing here reads input or prints, so every line can be tested. The player is
always told the battle from the point of view of `me`, the side they play.
"""

import re
from collections.abc import Callable
from dataclasses import dataclass
from typing import Protocol

from trainerlab import _engine

STATUS_NAMES = ["", "burned", "poisoned", "badly poisoned", "paralyzed", "asleep", "frozen"]
STATUS_TAGS = ["", "BRN", "PSN", "TOX", "PAR", "SLP", "FRZ"]
STAT_NAMES = ["Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed", "accuracy", "evasiveness"]
STAT_TAGS = ["Atk", "Def", "SpA", "SpD", "Spe", "Acc", "Eva"]
WEATHER_NAMES = ["", "rain", "harsh sunlight", "a sandstorm", "snow", "strong winds"]
TERRAIN_NAMES = ["", "Electric Terrain"]
HAZARD_NAMES = ["Stealth Rock", "Spikes", "Toxic Spikes"]
SKIP_REASONS = ["is fast asleep", "is frozen solid", "is fully paralyzed", "flinched"]
STRUGGLE, STRUGGLE_RECOIL = -2, -3
CATEGORY_NAMES = ["physical", "special", "status"]
STATUS_CATEGORY = 2
# Names whose punctuation a capital-letter split cannot guess.
SPECIAL_NAMES = {"UTurn": "U-turn", "WillOWisp": "Will-O-Wisp"}
EFFECTIVENESS_TAGS = {0.0: "x0", 0.25: "x1/4", 0.5: "x1/2", 2.0: "x2", 4.0: "x4"}
STATUS_VERBS = ["", "burn", "poison", "badly poison", "paralyze", "put to sleep", "freeze"]


class EventFields(Protocol):
    """The fields of an engine event that the text needs (README.md of the engine, section 6)."""

    @property
    def kind(self) -> int: ...
    @property
    def side(self) -> int: ...
    @property
    def slot(self) -> int: ...
    @property
    def name_id(self) -> int: ...
    @property
    def i0(self) -> int: ...
    @property
    def i1(self) -> int: ...
    @property
    def f0(self) -> float: ...
    @property
    def flags(self) -> int: ...


# Event kinds that take HP away, the one that gives it back, and the others that matter here.
_HP_LOSSES = {1, 6, 17, 19, 23, 28, 35}
_HEALED = 22
_MOVE_USED, _DAMAGE, _FAINTED, _SWITCHED_OUT = 0, 1, 2, 11


@dataclass(frozen=True)
class Narration:
    """One line of a turn's history, with what the line is about.

    `emphasis` marks the moments worth a pause (knock-outs, critical hits, super effective
    hits); `amount` is the HP really lost or restored, never more than the Pokemon had.
    """

    text: str
    emphasis: bool
    kind: int
    side: int
    slot: int
    amount: int


class Style:
    """ANSI colours, or plain text when disabled (not a terminal, or --no-color)."""

    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def mine(self, text: str) -> str:
        return self._wrap(text, "32")  # green

    def theirs(self, text: str) -> str:
        return self._wrap(text, "31")  # red

    def bold(self, text: str) -> str:
        return self._wrap(text, "1")

    def dim(self, text: str) -> str:
        return self._wrap(text, "2")

    def hp(self, text: str, ratio: float) -> str:
        """Green above half HP, yellow above a fifth, red below."""
        return self._wrap(text, "32" if ratio > 0.5 else "33" if ratio > 0.2 else "31")

    def _wrap(self, text: str, code: str) -> str:
        return f"\033[{code}m{text}\033[0m" if self.enabled else text


def readable(name: str) -> str:
    """'DragonDarts' -> 'Dragon Darts', 'UTurn' -> 'U-turn'."""
    return SPECIAL_NAMES.get(name) or re.sub(r"(?<=[a-z])(?=[A-Z])", " ", name)


def move_label(move_id: int) -> str:
    return "Struggle" if move_id == STRUGGLE else readable(_engine.move_name(move_id))


def pokemon_label(view: _engine.BattleState, side: int, slot: int, me: int, style: Style) -> str:
    name = _engine.species_display_name(view.pokemon(side, slot).species_id)
    return style.mine(f"Your {name}") if side == me else style.theirs(f"The foe's {name}")


def team_label(side: int, me: int) -> str:
    return "your team" if side == me else "the foe's team"


def hp_bar(current: int, maximum: int, width: int = 20) -> str:
    filled = round(width * current / maximum) if current > 0 else 0
    return "[" + "#" * filled + "." * (width - filled) + "]"


def species_types(species_id: int) -> str:
    entry = _engine.species_entry(species_id)
    first, second = _type_name(entry.type1), _type_name(entry.type2)
    return first if first == second else f"{first}/{second}"


def describe_pokemon(pokemon: _engine.BattlePokemon, style: Style) -> list[str]:
    """Two lines: name, types, status and stat stages; then the HP bar and numbers."""
    name = _engine.species_display_name(pokemon.species_id)
    parts = [style.bold(name), species_types(pokemon.species_id)]
    if pokemon.status:
        parts.append(STATUS_TAGS[pokemon.status])
    stages = [
        f"{stage:+d} {STAT_TAGS[stat]}" for stat, stage in enumerate(pokemon.stat_stages) if stage
    ]
    if stages:
        parts.append(", ".join(stages))
    ratio = pokemon.current_hp / pokemon.stats.hp
    bar = style.hp(hp_bar(pokemon.current_hp, pokemon.stats.hp), ratio)
    return ["  ".join(parts), f"{bar} {pokemon.current_hp}/{pokemon.stats.hp} HP"]


def describe_field(view: _engine.BattleState, me: int) -> list[str]:
    """Weather, terrain, screens and hazards, only those in play."""
    lines = []
    if view.weather:
        lines.append(f"Weather: {WEATHER_NAMES[view.weather]}")
    if view.terrain:
        lines.append(f"Terrain: {TERRAIN_NAMES[view.terrain]}")
    for side in (me, 1 - me):
        hazards = view.hazards(side)
        layers = [
            (HAZARD_NAMES[0], hazards.stealth_rock),
            (HAZARD_NAMES[1], hazards.spikes),
            (HAZARD_NAMES[2], hazards.toxic_spikes),
        ]
        present = [name if count == 1 else f"{name} x{count}" for name, count in layers if count]
        if view.aurora_veil_turns[side]:
            present.append("Aurora Veil")
        if present:
            lines.append(f"On {team_label(side, me)}'s side: {', '.join(present)}")
    return lines


def describe_board(view: _engine.BattleState, me: int, style: Style) -> list[str]:
    """What the player sees between two turns: both actives, the bench and the field."""
    foe = 1 - me
    team = [view.pokemon(foe, i) for i in range(view.team_size[foe])]
    revealed = [pokemon for pokemon in team if not pokemon.is_empty()]
    standing = sum(1 for pokemon in revealed if not pokemon.is_fainted())
    unseen = view.team_size[foe] - len(revealed)
    foe_name, foe_hp = describe_pokemon(view.active(foe), style)
    my_name, my_hp = describe_pokemon(view.active(me), style)
    bench = [
        f"{_engine.species_display_name(p.species_id)} {p.current_hp}/{p.stats.hp}"
        for i in range(view.team_size[me])
        if i != view.active_index[me] and not (p := view.pokemon(me, i)).is_fainted()
    ]
    lines = [
        style.theirs("Foe ") + " " + foe_name,
        "     " + foe_hp,
        "     " + style.dim(f"{standing} seen standing, {unseen} not yet seen"),
        "",
        style.mine("You ") + " " + my_name,
        "     " + my_hp,
        "     " + style.dim("bench: " + (", ".join(bench) if bench else "none")),
    ]
    field = describe_field(view, me)
    return lines + ([""] + field if field else [])


def describe_move(
    action: _engine.Action, view: _engine.BattleState, me: int, hints: bool = False
) -> str:
    """A menu entry for a move action: its type, power and PP.

    With `hints`, a damaging move also shows its type effectiveness on the foe's active, which
    a player can work out from the type chart anyway.
    """
    active = view.active(me)
    move_id = active.move_ids[action.index]
    if active.charging_move_id != _engine.NO_MOVE:
        return f"Unleash {move_label(active.charging_move_id)}"
    if active.pp[action.index] == 0:
        return "Struggle (no PP left)"
    move = _engine.move_entry(move_id)
    pp = f"{active.pp[action.index]}/{move.pp} PP"
    label = f"{move_label(move_id)} ({_type_name(move.type)}, {_power_label(move)}, {pp})"
    if hints and move.category != STATUS_CATEGORY:
        effectiveness = _engine.type_effectiveness(move.type, view.active(1 - me).species_id)
        # Fixed damage ignores type effectiveness, except an immunity.
        if not _fixed_damage(move) or effectiveness == 0:
            tag = EFFECTIVENESS_TAGS.get(effectiveness)
            if tag:
                label += f"  {tag}"
    return label


def describe_switch(
    team_index: int, view: _engine.BattleState, me: int, hints: bool = False
) -> str:
    """A menu entry for a Pokemon that can come in: its types, HP and status.

    With `hints`, it also says how it takes the foe's types: weak, resistant or immune.
    """
    pokemon = view.pokemon(me, team_index)
    name = _engine.species_display_name(pokemon.species_id)
    details = [species_types(pokemon.species_id), f"{pokemon.current_hp}/{pokemon.stats.hp} HP"]
    if pokemon.status:
        details.append(STATUS_TAGS[pokemon.status])
    label = f"{name} ({', '.join(details)})"
    matchup = _against_types(pokemon.species_id, view.active(1 - me).species_id)
    return label + f"  {matchup}" if hints and matchup else label


def _against_types(species_id: int, foe_species_id: int) -> str:
    """How a species takes each of the foe's types: 'weak to Fighting, immune to Ghost'."""
    foe = _engine.species_entry(foe_species_id)
    remarks = []
    for foe_type in dict.fromkeys((foe.type1, foe.type2)):
        effectiveness = _engine.type_effectiveness(foe_type, species_id)
        if effectiveness == 0:
            remarks.append(f"immune to {_type_name(foe_type)}")
        elif effectiveness > 1:
            remarks.append(f"weak to {_type_name(foe_type)}")
        elif effectiveness < 1:
            remarks.append(f"resists {_type_name(foe_type)}")
    return ", ".join(remarks)


def _fixed_damage(move: _engine.MoveEntry) -> bool:
    return any(effect.kind == "FixedDamage" for effect in move.effects)


def _power_label(move: _engine.MoveEntry) -> str:
    if move.power:
        return f"{move.power} power"
    if move.power_from_target_weight:
        return "power from weight"
    return "fixed damage" if _fixed_damage(move) else "status"


def describe_moves(view: _engine.BattleState, me: int) -> list[str]:
    """The details of the active Pokemon's moves, for the help screen."""
    active = view.active(me)
    lines = []
    for slot, move_id in enumerate(active.move_ids):
        move = _engine.move_entry(move_id)
        accuracy = f"{move.accuracy}% accuracy" if move.accuracy > 0 else "never misses"
        priority = f", priority {move.priority:+d}" if move.priority else ""
        lines.append(
            f"{move_label(move_id)}: {_type_name(move.type)}, {CATEGORY_NAMES[move.category]}, "
            f"{_power_label(move)}, {accuracy}{priority}, {active.pp[slot]}/{move.pp} PP"
        )
        effects = describe_effects(move)
        if effects:
            lines.append("  " + effects)
    return lines


def describe_effects(move: _engine.MoveEntry) -> str:
    """What a move does besides plain damage, in one sentence: '30% chance to paralyze'."""
    phrases = []
    for effect in move.effects:
        if effect.kind == "Damage":
            continue
        phrase = EFFECT_PHRASES[effect.kind](effect)
        if effect.chance < 100:
            phrase = f"{effect.chance}% chance to {phrase}"
        phrases.append(phrase)
    text = ", ".join(phrases)
    return text[:1].upper() + text[1:] + ("." if text else "")


def _stat_change(effect: _engine.EffectEntry) -> str:
    verb = "raise" if effect.delta > 0 else "lower"
    whose = "the user's" if effect.affects_user else "the target's"
    stages = abs(effect.delta)
    return f"{verb} {whose} {STAT_NAMES[effect.stat]} by {stages} stage{'s' if stages > 1 else ''}"


EFFECT_PHRASES: dict[str, Callable[[_engine.EffectEntry], str]] = {
    "ApplyStatus": lambda e: STATUS_VERBS[e.status] + " the target",
    "StatChange": _stat_change,
    "Flinch": lambda e: "make the target flinch",
    "Recoil": lambda e: f"take 1/{e.denominator} of the damage dealt as recoil",
    "Drain": lambda e: f"heal 1/{e.denominator} of the damage dealt",
    "Recovery": lambda e: f"heal 1/{e.denominator} of max HP",
    "FixedDamage": lambda e: (
        "deal damage equal to the user's level"
        if e.damage_equals_level
        else "halve the target's HP"
    ),
    "MultiHit": lambda e: "hit several times",
    "Pivot": lambda e: "switch the user out after the move",
    "ForceSwitch": lambda e: "force the target out",
    "KnockOff": lambda e: "knock off the target's item",
    "SetHazard": lambda e: f"set {HAZARD_NAMES[e.hazard]} around the foe",
    "HazardOnHit": lambda e: "set Spikes around the foe on hit",
    "ClearHazards": lambda e: "clear hazards" + (" on both sides" if e.both_sides else ""),
    "SetScreen": lambda e: "set Aurora Veil for the user's team",
    # Baneful Bunker is the catalog's only protection, and it always poisons on contact.
    "Protect": lambda e: (
        f"protect the user this turn and {STATUS_VERBS[e.status]} an attacker making contact"
    ),
    "Roost": lambda e: "lose the Flying type for the turn",
    "Rest": lambda e: "sleep two turns and heal fully",
    "Wish": lambda e: "heal half max HP at the end of the next turn",
    "StealBoosts": lambda e: "steal the target's stat boosts",
    "SleepTalk": lambda e: "use another of its moves at random while asleep",
    "DestinyBond": lambda e: "take the attacker down if the user faints this turn",
    "BellyDrum": lambda e: "halve the user's HP to maximize its Attack",
}


def narrate(
    events: list[EventFields],
    before: _engine.BattleState,
    after: _engine.BattleState,
    me: int,
    style: Style,
) -> list[Narration]:
    """The turn's history, from the views before and after it.

    The engine reports raw damage, which can exceed the HP left (README.md of the engine,
    section 6): HP are followed through the turn so that each line shows what was really lost.
    A fainted Pokemon leaving the field is not told: its knock-out already was.
    """
    hp: dict[tuple[int, int], int] = {}
    lines = []
    for event in events:
        if event.kind in FIELD_EVENTS:
            text = FIELD_EVENTS[event.kind](event, me)
            lines.append(Narration(text, False, event.kind, event.side, event.slot, 0))
            continue
        key = (event.side, event.slot)
        if key not in hp:
            hp[key] = _hp_before(before, after, event.side, event.slot)
        if event.kind == _SWITCHED_OUT and hp[key] == 0:
            continue
        amount = event.i0
        if event.kind in _HP_LOSSES:
            amount = min(amount, hp[key])
            hp[key] -= amount
        elif event.kind == _HEALED:
            hp[key] += amount
        who = pokemon_label(after, event.side, event.slot, me, style)
        text = POKEMON_EVENTS[event.kind](event, who, amount)
        emphasis = event.kind == _FAINTED or (
            event.kind == _DAMAGE and (bool(event.flags & _engine.FLAG_CRIT) or event.f0 > 1)
        )
        lines.append(Narration(text, emphasis, event.kind, event.side, event.slot, amount))
    return lines


def describe_events(
    events: list[EventFields],
    before: _engine.BattleState,
    after: _engine.BattleState,
    me: int,
    style: Style,
) -> list[str]:
    """The turn's history as plain lines (see `narrate`)."""
    return [line.text for line in narrate(events, before, after, me, style)]


def _hp_before(
    before: _engine.BattleState, after: _engine.BattleState, side: int, slot: int
) -> int:
    pokemon = before.pokemon(side, slot)
    # A foe that had never been seen enters at full HP.
    return after.pokemon(side, slot).stats.hp if pokemon.is_empty() else pokemon.current_hp


def _type_name(type_id: int) -> str:
    return [
        "Normal", "Fire", "Water", "Electric", "Grass", "Ice", "Fighting", "Poison", "Ground",
        "Flying", "Psychic", "Bug", "Rock", "Ghost", "Dragon", "Dark", "Steel", "Fairy",
    ][type_id]  # fmt: skip


def _damage(event: EventFields, who: str, amount: int) -> str:
    text = f"{who} lost {amount} HP."
    if event.flags & _engine.FLAG_CRIT:
        text = "A critical hit! " + text
    if event.f0 > 1:
        text += " It's super effective!"
    elif 0 < event.f0 < 1:
        text += " It's not very effective..."
    return text


def _stage_change(event: EventFields, who: str, amount: int) -> str:
    size = {1: "rose", 2: "rose sharply", -1: "fell", -2: "harshly fell"}
    verb = size.get(event.i0, "rose drastically" if event.i0 > 0 else "severely fell")
    return f"{who}'s {STAT_NAMES[event.i1]} {verb}!"


def _direction(event: EventFields) -> str:
    return "higher" if event.i1 else "lower"


def _ability(event: EventFields) -> str:
    if event.name_id == STRUGGLE_RECOIL:
        return "struggle recoil"
    return readable(_engine.ability_name(event.name_id))


def _item(event: EventFields) -> str:
    return readable(_engine.item_name(event.name_id))


POKEMON_EVENTS: dict[int, Callable[[EventFields, str, int], str]] = {
    0: lambda e, who, n: f"{who} used {move_label(e.name_id)}!",
    1: _damage,
    2: lambda e, who, n: f"{who} fainted!",
    3: lambda e, who, n: f"{who}'s attack missed!",
    4: lambda e, who, n: f"{who} is now {STATUS_NAMES[e.i0]}!",
    5: lambda e, who, n: f"{who} could not be {STATUS_NAMES[e.i0]}.",
    6: lambda e, who, n: f"{who} is hurt by being {STATUS_NAMES[e.i1]} ({n} HP).",
    7: lambda e, who, n: f"{who} is no longer {STATUS_NAMES[e.i0]}.",
    8: lambda e, who, n: f"{who} {SKIP_REASONS[e.i0]}!",
    9: _stage_change,
    10: lambda e, who, n: f"{who}'s {STAT_NAMES[e.i0]} won't go any {_direction(e)}!",
    11: lambda e, who, n: f"{who} withdrew.",
    12: lambda e, who, n: f"{who} entered the battle!",
    13: lambda e, who, n: f"{who}'s {_ability(e)} activated!",
    14: lambda e, who, n: f"{who} used {move_label(e.name_id)}, but it failed!",
    17: lambda e, who, n: f"{who} is buffeted by {WEATHER_NAMES[e.i1]} ({n} HP).",
    19: lambda e, who, n: f"{who} is hurt by {HAZARD_NAMES[e.i1]} ({n} HP).",
    21: lambda e, who, n: f"{who} absorbed the Toxic Spikes.",
    22: lambda e, who, n: f"{who} restored {n} HP.",
    23: lambda e, who, n: f"{who} is hit with recoil ({n} HP).",
    24: lambda e, who, n: f"{who} is charging {move_label(e.name_id)}!",
    25: lambda e, who, n: f"{who} protected itself!",
    26: lambda e, who, n: f"{who}'s {_item(e)} activated!",
    27: lambda e, who, n: f"{who} used up its {_item(e)}.",
    28: lambda e, who, n: f"{who} is hurt by its {_item(e)} ({n} HP).",
    29: lambda e, who, n: f"{who} lost its {_item(e)}!",
    34: lambda e, who, n: f"{who} took its attacker down with it!",
    35: lambda e, who, n: f"{who} is hurt by {_ability(e)} ({n} HP).",
}

FIELD_EVENTS: dict[int, Callable[[EventFields, int], str]] = {
    15: lambda e, me: f"It started: {WEATHER_NAMES[e.i0]}.",
    16: lambda e, me: f"The {WEATHER_NAMES[e.i0]} stopped.",
    18: lambda e, me: f"{HAZARD_NAMES[e.i0]} now lies around {team_label(e.side, me)}.",
    20: lambda e, me: f"The hazards around {team_label(e.side, me)} are gone.",
    30: lambda e, me: f"{TERRAIN_NAMES[e.i0]} covers the field.",
    31: lambda e, me: f"The {TERRAIN_NAMES[e.i0]} faded.",
    32: lambda e, me: f"Aurora Veil protects {team_label(e.side, me)} for {e.i0} turns.",
    33: lambda e, me: f"Aurora Veil around {team_label(e.side, me)} wore off.",
}
