"""Turns battle views and events into what a player reads: the board, the options, the turn.

Pure functions: nothing here reads input or prints, so every line can be tested. Texts carry
rich's markup ([red]...[/]) when colours are on, plain text otherwise; the words come from
trainerlab.i18n, in the language chosen. The battle is told from the side of `me`.
"""

import json
from collections.abc import Callable
from dataclasses import dataclass
from functools import cache
from typing import Protocol

from rich.table import Table
from rich.text import Text

from trainerlab import _engine
from trainerlab.game.environment import DATA_DIR
from trainerlab.game.events import EventKind as K
from trainerlab.i18n import names, t
from trainerlab.terminal.prompts import Label, Option

STATUS_CATEGORY = 2
HP_WIDTH = 24
ITEM = "◆"  # after the name of a Pokemon holding an item
# The colour of each type, in the engine's order (names.TYPES), as the games draw them.
TYPE_COLOURS = [
    "#A8A878", "#F08030", "#6890F0", "#F8D030", "#78C850", "#98D8D8", "#C03028", "#A040A0",
    "#E0C068", "#A890F0", "#F85888", "#A8B820", "#B8A038", "#705898", "#7038F8", "#705848",
    "#B8B8D0", "#EE99AC",
]  # fmt: skip


class EventFields(Protocol):
    """The fields of an engine event that the text needs (the event table of engine/docs/api.md)."""

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


# The event kinds that take HP away.
HP_LOSSES = {
    K.DAMAGE_DEALT,
    K.STATUS_DAMAGE,
    K.WEATHER_DAMAGE,
    K.HAZARD_DAMAGE,
    K.RECOIL_DAMAGE,
    K.ITEM_DAMAGE,
    K.ABILITY_DAMAGE,
}


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
    """rich's markup, or plain text when disabled (not a terminal, or --no-color)."""

    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def mine(self, text: str) -> str:
        return self._wrap(text, "cyan")

    def theirs(self, text: str) -> str:
        return self._wrap(text, "red")

    def bold(self, text: str) -> str:
        return self._wrap(text, "bold")

    def dim(self, text: str) -> str:
        return self._wrap(text, "dim")

    def hp(self, text: str, ratio: float) -> str:
        """Green above half HP, yellow above a fifth, red below."""
        return self._wrap(text, hp_colour(ratio))

    def type_name(self, type_id: int) -> str:
        return self._wrap(names.type_name(type_id), TYPE_COLOURS[type_id])

    def _wrap(self, text: str, style: str) -> str:
        return f"[{style}]{text}[/]" if self.enabled else text


def plain(text: str) -> str:
    """A text without its markup: for the log file and for the tests."""
    return Text.from_markup(text).plain


def hp_colour(ratio: float) -> str:
    return "green" if ratio > 0.5 else "yellow" if ratio > 0.2 else "red"


def hp_bar(current: int, maximum: int, width: int = HP_WIDTH) -> str:
    filled = round(width * current / maximum) if current > 0 else 0
    return "█" * filled + "░" * (width - filled)


def hp_text(pokemon: _engine.BattlePokemon) -> str:
    return t("hp", hp=pokemon.current_hp, max=pokemon.stats.hp)


def holds_item(pokemon: _engine.BattlePokemon) -> bool:
    return pokemon.item_id != _engine.NO_ITEM and not pokemon.item_consumed


def name_with_item(pokemon: _engine.BattlePokemon) -> str:
    name = names.species(pokemon.species_id)
    return f"{name} {ITEM}" if holds_item(pokemon) else name


def species_types(species_id: int, style: Style) -> str:
    entry = _engine.species_entry(species_id)
    first, second = style.type_name(entry.type1), style.type_name(entry.type2)
    return first if entry.type1 == entry.type2 else f"{first} / {second}"


def species_label(key: str) -> str:
    """A species in a list to choose from: its name and its types, by the engine's id of it."""
    _engine.engine_init(str(DATA_DIR))
    species_id = _engine.find_species_id(key)
    return f"{names.species_name(key):<22}{species_types(species_id, Style(enabled=False))}"


@cache
def _species_data(key: str) -> dict[str, object]:
    loaded: dict[str, object] = json.loads((DATA_DIR / "pokemon" / f"{key}.json").read_text())
    return loaded


def species_card(key: str, *, holding: bool = True) -> str:
    """A species, under a list: its types, base stats, ability, item and moves, with what the
    ability and the item do. `holding` False leaves out an item used up in battle."""
    _engine.engine_init(str(DATA_DIR))
    species_id = _engine.find_species_id(key)
    data = _species_data(key)
    base: dict[str, int] = data["baseStats"]  # type: ignore[assignment]
    stats = t(
        "card.stats",
        hp=base["hp"], atk=base["atk"], df=base["def"],
        spa=base["specAtk"], spd=base["specDef"], spe=base["speed"],
    )  # fmt: skip
    ability = str(data["ability"])
    lines = [species_types(species_id, Style(enabled=False)), stats]
    if ability:
        ability_name = names.ability(_engine.find_ability_id(ability))
        lines.append(f"{ability_name}: {names.ability_effect(ability)}")
    item = data.get("item")
    if item and holding:
        item_name = names.item(_engine.find_item_id(str(item)))
        lines.append(f"{ITEM} {item_name}: {names.item_effect(str(item))}")
    for move_id in _engine.make_combatant(species_id).move_ids:
        move = _engine.move_entry(move_id)
        category = t(f"category.{move.category}")
        lines.append(f"{names.move(move_id)} · {names.type_name(move.type)} · {category}")
    return card(names.species_name(key), lines)


def team_dots(view: _engine.BattleState, side: int) -> str:
    """● per Pokemon standing (an unseen foe stands), ○ per Pokemon knocked out."""
    team = [view.pokemon(side, i) for i in range(view.team_size[side])]
    fainted = sum(1 for p in team if not p.is_empty() and p.is_fainted())
    return "●" * (len(team) - fainted) + "○" * fainted


def _details(pokemon: _engine.BattlePokemon) -> str:
    """The status and the stat stages of a Pokemon: 'PAR · Atk +2 · Spe -1'."""
    parts = [t(f"status_tag.{pokemon.status}")] if pokemon.status else []
    parts += [
        f"{t(f'stat_tag.{stat}')} {stage:+d}"
        for stat, stage in enumerate(pokemon.stat_stages)
        if stage
    ]
    return " · ".join(parts)


Trainers = tuple[str | None, str | None]  # who plays each side; None: the player at the keyboard


def board(view: _engine.BattleState, me: int, style: Style, trainers: Trainers) -> Table:
    """Both actives, the foe's above: who plays them, name, item, level, HP and team; under
    each, its status and stat stages; then the field."""
    labels = [t("you") if trainer is None else trainer for trainer in trainers]
    grid = Table.grid(padding=(0, 2))
    for _ in range(6):
        grid.add_column()
    for side, paint in ((1 - me, style.theirs), (me, style.mine)):
        label = labels[side]
        pokemon = view.active(side)
        ratio = pokemon.current_hp / pokemon.stats.hp
        grid.add_row(
            paint(label),
            style.bold(name_with_item(pokemon)),
            t("level", level=pokemon.level),
            style.hp(hp_bar(pokemon.current_hp, pokemon.stats.hp), ratio),
            hp_text(pokemon),
            team_dots(view, side),
        )
        details = _details(pokemon)
        if details:
            grid.add_row("", style.dim(details), "", "", "", "")
    for line in describe_field(view, me, trainers):
        grid.add_row("", style.dim(line), "", "", "", "")
    return grid


def describe_field(view: _engine.BattleState, me: int, trainers: Trainers) -> list[str]:
    """Weather, terrain, Aurora Veil and hazards, only those in play."""
    lines = []
    if view.weather:
        lines.append(t("field.weather", weather=t(f"weather.{view.weather}")))
    if view.terrain:
        lines.append(t("field.terrain", terrain=t(f"terrain.{view.terrain}")))
    for side in (me, 1 - me):
        hazards = view.hazards(side)
        layers = [hazards.stealth_rock, hazards.spikes, hazards.toxic_spikes]
        present = [
            t(f"hazard.{kind}") + (f" ×{count}" if count > 1 else "")
            for kind, count in enumerate(layers)
            if count
        ]
        if view.aurora_veil_turns[side]:
            present.append(t("aurora_veil"))
        if present:
            trainer, what = trainers[side], ", ".join(present)
            lines.append(
                t("field.yours", what=what)
                if trainer is None
                else t("field.theirs", trainer=trainer, what=what)
            )
    return lines


def card(title: str, lines: list[str], width: int = 58) -> str:
    """A framed text, shown under a list while an option is highlighted."""
    inner = width - 4
    top = f"╭─ {title} " + "─" * max(0, width - 5 - len(title)) + "╮"
    rows = []
    for line in lines:
        words, row = line.split(), ""
        for word in words:
            if row and len(row) + 1 + len(word) > inner:
                rows.append(row)
                row = word
            else:
                row = f"{row} {word}" if row else word
        rows.append(row)
    body = [f"│ {row:<{inner}} │" for row in rows]
    return "\n".join([top, *body, "╰" + "─" * (width - 2) + "╯"])


def move_card(move_id: int, pp: int) -> str:
    move = _engine.move_entry(move_id)
    power = str(move.power) if move.power else "—"
    accuracy = f"{move.accuracy} %" if move.accuracy > 0 else t("never_misses")
    numbers = t("move.numbers", power=power, accuracy=accuracy, pp=pp, max=move.pp)
    return card(names.move(move_id), [numbers, names.description(move_id)])


def move_option(action: _engine.Action, view: _engine.BattleState, me: int) -> Option:
    """A move in the battle menu: its name, type and category, and its card under the list."""
    active = view.active(me)
    move_id = active.move_ids[action.index]
    if active.charging_move_id != _engine.NO_MOVE:
        move_id = active.charging_move_id
    elif active.pp[action.index] == 0:
        return Option(
            action,
            names.move(_engine.STRUGGLE),
            card(names.move(_engine.STRUGGLE), [t("move.struggle_card")]),
        )
    move = _engine.move_entry(move_id)
    label: Label = [
        ("bold", f"{names.move(move_id):<17}"),
        (f"fg:{TYPE_COLOURS[move.type]}", f"{names.type_name(move.type):<10}"),
        ("", t(f"category.{move.category}")),
    ]
    return Option(action, label, move_card(move_id, active.pp[action.index]))


def switch_options(view: _engine.BattleState, me: int, legal: list[int]) -> list[Option]:
    """Every Pokemon of the team, in order; those that cannot come in are greyed out."""
    options = []
    plain_style = Style(enabled=False)
    for index in range(view.team_size[me]):
        pokemon = view.pokemon(me, index)
        name = f"{name_with_item(pokemon):<14}"
        types = f"{species_types(pokemon.species_id, plain_style):<17}"
        if pokemon.is_fainted():
            options.append(Option(index, name + types, disabled=t("fainted")))
            continue
        if index == view.active_index[me] or index not in legal:
            options.append(Option(index, name + types, disabled=t("in_battle")))
            continue
        ratio = pokemon.current_hp / pokemon.stats.hp
        status = f"  {t(f'status_tag.{pokemon.status}')}" if pokemon.status else ""
        label: Label = [
            ("bold", name),
            ("", types),
            (f"fg:ansi{hp_colour(ratio)}", hp_bar(pokemon.current_hp, pokemon.stats.hp)),
            ("", f"  {hp_text(pokemon)}{status}"),
        ]
        key = _engine.species_id_string(pokemon.species_id)
        options.append(Option(index, label, species_card(key, holding=holds_item(pokemon))))
    return options


class Teller:
    """Tells the events of a battle from `me`'s side: whose Pokemon, its trainer, the HP.

    `trainers` names who plays each side; None is the player at the keyboard, told in the
    second person ("Go, Gyarados!").
    """

    def __init__(self, me: int, style: Style, trainers: Trainers) -> None:
        self.me, self.style, self.trainers = me, style, trainers

    def who(self, view: _engine.BattleState, side: int, slot: int) -> str:
        name = names.species(view.pokemon(side, slot).species_id)
        return self.style.mine(name) if side == self.me else self.style.theirs(name)

    def team(self, side: int) -> str:
        trainer = self.trainers[side]
        return t("team.yours") if trainer is None else t("team.of", trainer=trainer)

    def narrate(
        self,
        events: list[EventFields],
        before: _engine.BattleState,
        after: _engine.BattleState,
    ) -> list[Narration]:
        """The turn's history, from the views before and after it.

        The engine reports raw damage, which can exceed the HP left (engine/docs/api.md): HP are
        followed through the turn so that each line shows what was really lost. A fainted Pokemon
        leaving the field is not told: its knock-out already was.
        """
        hp: dict[tuple[int, int], int] = {}
        lines = []
        for event in events:
            if event.kind in FIELD_EVENTS:
                text = FIELD_EVENTS[event.kind](self, event)
                lines.append(Narration(text, False, event.kind, event.side, event.slot, 0))
                continue
            key = (event.side, event.slot)
            if key not in hp:
                hp[key] = _hp_before(before, after, event.side, event.slot)
            if event.kind == K.SWITCHED_OUT and hp[key] == 0:
                continue
            amount = event.i0
            if event.kind in HP_LOSSES:
                amount = min(amount, hp[key])
                hp[key] -= amount
            elif event.kind == K.HEALED:
                hp[key] += amount
            who = self.who(after, event.side, event.slot)
            if event.kind in (K.SWITCHED_IN, K.SWITCHED_OUT):
                text = self._switch(event, who)
            else:
                text = POKEMON_EVENTS[event.kind](event, who, amount)
            crit = bool(event.flags & _engine.FLAG_CRIT)
            emphasis = event.kind == K.FAINTED or (
                event.kind == K.DAMAGE_DEALT and (crit or event.f0 > 1)
            )
            lines.append(Narration(text, emphasis, event.kind, event.side, event.slot, amount))
        return lines

    def _switch(self, event: EventFields, who: str) -> str:
        trainer = self.trainers[event.side]
        coming = event.kind == K.SWITCHED_IN
        if trainer is None:
            return t("event.go" if coming else "event.come_back", who=who)
        return t("event.sends_out" if coming else "event.withdraws", trainer=trainer, who=who)


def _hp_before(
    before: _engine.BattleState, after: _engine.BattleState, side: int, slot: int
) -> int:
    pokemon = before.pokemon(side, slot)
    # A foe that had never been seen enters at full HP.
    return after.pokemon(side, slot).stats.hp if pokemon.is_empty() else pokemon.current_hp


def _damage(event: EventFields, who: str, amount: int) -> str:
    text = t("event.damage", who=who, hp=amount)
    if event.flags & _engine.FLAG_CRIT:
        text = t("event.crit") + " " + text
    if event.f0 > 1:
        text += " " + t("event.super_effective")
    elif 0 < event.f0 < 1:
        text += " " + t("event.not_very_effective")
    return text


def _stage_change(event: EventFields, who: str, amount: int) -> str:
    size = max(-3, min(3, event.i0))
    return t("event.stage", who=who, stat=t(f"stat.{event.i1}"), change=t(f"stage.{size}"))


def _ability(event: EventFields) -> str:
    if event.name_id == _engine.STRUGGLE_RECOIL:
        return t("struggle_recoil")
    return names.ability(event.name_id)


def _item(event: EventFields) -> str:
    return names.item(event.name_id)


def _status(event: EventFields, field: int) -> str:
    return t(f"status.{field}")


POKEMON_EVENTS: dict[int, Callable[[EventFields, str, int], str]] = {
    K.MOVE_USED: lambda e, who, n: t("event.move", who=who, move=names.move(e.name_id)),
    K.DAMAGE_DEALT: _damage,
    K.FAINTED: lambda e, who, n: t("event.fainted", who=who),
    K.MISSED: lambda e, who, n: t("event.missed", who=who),
    K.STATUS_APPLIED: lambda e, who, n: t("event.status", who=who, status=_status(e, e.i0)),
    K.STATUS_FAILED: lambda e, who, n: t("event.status_failed", who=who, status=_status(e, e.i0)),
    K.STATUS_DAMAGE: lambda e, who, n: t(
        "event.status_damage", who=who, status=_status(e, e.i1), hp=n
    ),
    K.STATUS_CURED: lambda e, who, n: t("event.status_cured", who=who, status=_status(e, e.i0)),
    K.MOVE_SKIPPED: lambda e, who, n: t(f"event.skip.{e.i0}", who=who),
    K.STAT_STAGE_CHANGED: _stage_change,
    K.STAT_CHANGE_FAILED: lambda e, who, n: t(
        "event.stage_failed_up" if e.i1 else "event.stage_failed_down",
        who=who,
        stat=t(f"stat.{e.i0}"),
    ),
    K.ABILITY_TRIGGERED: lambda e, who, n: t("event.ability", who=who, ability=_ability(e)),
    K.MOVE_FAILED: lambda e, who, n: t("event.move_failed", who=who, move=names.move(e.name_id)),
    K.WEATHER_DAMAGE: lambda e, who, n: t(
        "event.weather_damage", who=who, weather=t(f"weather.{e.i1}"), hp=n
    ),
    K.HAZARD_DAMAGE: lambda e, who, n: t(
        "event.hazard_damage", who=who, hazard=t(f"hazard.{e.i1}"), hp=n
    ),
    K.TOXIC_SPIKES_ABSORBED: lambda e, who, n: t("event.toxic_spikes_absorbed", who=who),
    K.HEALED: lambda e, who, n: t("event.healed", who=who, hp=n),
    K.RECOIL_DAMAGE: lambda e, who, n: t("event.recoil", who=who, hp=n),
    K.CHARGING: lambda e, who, n: t("event.charging", who=who, move=names.move(e.name_id)),
    K.PROTECTED: lambda e, who, n: t("event.protected", who=who),
    K.ITEM_TRIGGERED: lambda e, who, n: t("event.item", who=who, item=_item(e)),
    K.ITEM_CONSUMED: lambda e, who, n: t("event.item_consumed", who=who, item=_item(e)),
    K.ITEM_DAMAGE: lambda e, who, n: t("event.item_damage", who=who, item=_item(e), hp=n),
    K.ITEM_KNOCKED_OFF: lambda e, who, n: t("event.knocked_off", who=who, item=_item(e)),
    K.DESTINY_BOND_TRIGGERED: lambda e, who, n: t("event.destiny_bond", who=who),
    K.ABILITY_DAMAGE: lambda e, who, n: t(
        "event.ability_damage", who=who, ability=_ability(e), hp=n
    ),
}

FIELD_EVENTS: dict[int, Callable[[Teller, EventFields], str]] = {
    K.WEATHER_STARTED: lambda teller, e: t("event.weather", weather=t(f"weather.{e.i0}")),
    K.WEATHER_ENDED: lambda teller, e: t("event.weather_ended", weather=t(f"weather.{e.i0}")),
    K.HAZARD_SET: lambda teller, e: t(
        "event.hazard_set", hazard=t(f"hazard.{e.i0}"), team=teller.team(e.side)
    ),
    K.HAZARDS_CLEARED: lambda teller, e: t("event.hazards_cleared", team=teller.team(e.side)),
    K.TERRAIN_STARTED: lambda teller, e: t("event.terrain", terrain=t(f"terrain.{e.i0}")),
    K.TERRAIN_ENDED: lambda teller, e: t("event.terrain_ended", terrain=t(f"terrain.{e.i0}")),
    K.SCREEN_STARTED: lambda teller, e: t("event.screen", team=teller.team(e.side), turns=e.i0),
    K.SCREEN_ENDED: lambda teller, e: t("event.screen_ended", team=teller.team(e.side)),
}
