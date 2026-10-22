"""Turns battle views and events into text for a human player.

Pure functions: nothing here reads input or prints, so every line can be tested. The player is
always told the battle from the point of view of `me`, the side they play.
"""

import re
from collections.abc import Callable
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


# Event kinds that take HP away, and the one that gives it back.
_HP_LOSSES = {1, 6, 17, 19, 23, 28, 35}
_HEALED = 22


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

    def _wrap(self, text: str, code: str) -> str:
        return f"\033[{code}m{text}\033[0m" if self.enabled else text


def readable(name: str) -> str:
    """'DragonDarts' -> 'Dragon Darts'."""
    return re.sub(r"(?<=[a-z])(?=[A-Z])", " ", name)


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


def describe_pokemon(pokemon: _engine.BattlePokemon, style: Style) -> str:
    """One line: name, HP bar and numbers, status and stat stages."""
    name = _engine.species_display_name(pokemon.species_id)
    hp = f"{hp_bar(pokemon.current_hp, pokemon.stats.hp)} {pokemon.current_hp}/{pokemon.stats.hp}"
    parts = [style.bold(name), hp]
    if pokemon.status:
        parts.append(STATUS_TAGS[pokemon.status])
    stages = [
        f"{stage:+d} {STAT_TAGS[stat]}" for stat, stage in enumerate(pokemon.stat_stages) if stage
    ]
    if stages:
        parts.append(", ".join(stages))
    return "  ".join(parts)


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
    """What the player sees between two turns."""
    foe = 1 - me
    revealed = [
        view.pokemon(foe, i)
        for i in range(view.team_size[foe])
        if not view.pokemon(foe, i).is_empty()
    ]
    standing = sum(1 for p in revealed if not p.is_fainted())
    unseen = view.team_size[foe] - len(revealed)
    lines = [
        style.dim(f"--- Turn {view.turn + 1} ---"),
        "Foe:  " + style.theirs(describe_pokemon(view.active(foe), style)),
        style.dim(f"      {standing} seen standing, {unseen} not yet seen"),
        "You:  " + style.mine(describe_pokemon(view.active(me), style)),
    ]
    bench = [
        f"{_engine.species_display_name(p.species_id)} {p.current_hp}/{p.stats.hp}"
        for i in range(view.team_size[me])
        if i != view.active_index[me] and not (p := view.pokemon(me, i)).is_fainted()
    ]
    lines.append(style.dim("      bench: " + (", ".join(bench) if bench else "none")))
    return lines + describe_field(view, me)


def describe_action(action: _engine.Action, view: _engine.BattleState, me: int) -> str:
    """A menu entry: a move with its type, power and PP, or a switch with the HP of who enters."""
    if action.kind == _engine.ACTION_SWITCH:
        pokemon = view.pokemon(me, action.index)
        name = _engine.species_display_name(pokemon.species_id)
        return f"Switch to {name} ({pokemon.current_hp}/{pokemon.stats.hp} HP)"
    active = view.active(me)
    move_id = active.move_ids[action.index]
    if active.charging_move_id != _engine.NO_MOVE:
        return f"Unleash {move_label(active.charging_move_id)}"
    if active.pp[action.index] == 0:
        return "Struggle (no PP left)"
    move = _engine.move_entry(move_id)
    power = f"{move.power} power" if move.power else "status"
    pp = f"{active.pp[action.index]}/{move.pp} PP"
    return f"{move_label(move_id)} ({_type_name(move.type)}, {power}, {pp})"


def describe_events(
    events: list[EventFields],
    before: _engine.BattleState,
    after: _engine.BattleState,
    me: int,
    style: Style,
) -> list[str]:
    """The turn's history, one line per event, from the views before and after it.

    The engine reports raw damage, which can exceed the HP left (README.md of the engine,
    section 6): HP are followed through the turn so that each line shows what was really lost.
    """
    hp: dict[tuple[int, int], int] = {}
    lines = []
    for event in events:
        if event.kind in FIELD_EVENTS:
            lines.append(FIELD_EVENTS[event.kind](event, me))
            continue
        key = (event.side, event.slot)
        if key not in hp:
            hp[key] = _hp_before(before, after, event.side, event.slot)
        amount = event.i0
        if event.kind in _HP_LOSSES:
            amount = min(amount, hp[key])
            hp[key] -= amount
        elif event.kind == _HEALED:
            hp[key] += amount
        who = pokemon_label(after, event.side, event.slot, me, style)
        lines.append(POKEMON_EVENTS[event.kind](event, who, amount))
    return lines


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
