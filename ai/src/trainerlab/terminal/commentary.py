"""A battle between two bots, told with what each bot saw and estimated before it chose.

At every decision: the bot's Pokemon against the foe's, who moves first and the foe's best hit,
then every option with the bot's damage estimate and the one it chose; then the events of the
turn, as they really happened. A mistake stands out where the estimate and the events disagree.
"""

from collections.abc import Callable

from trainerlab import _engine
from trainerlab.game import estimate
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.player import Bot
from trainerlab.terminal.render import (
    Style,
    describe_events,
    describe_move,
    describe_switch,
    title_for,
)

Write = Callable[[str], None]

CHOSEN = ">"
PLAIN = Style(enabled=False)


def replay(
    env: BattleEnvironment,
    bots: tuple[Bot, Bot],
    names: tuple[str, str],
    side: int,
    write: Write,
) -> int | None:
    """Plays the battle, telling it from `side`'s point of view; returns the winner."""
    before = env.observe(side)

    def tell(events: list[_engine.Event]) -> None:
        nonlocal before
        after = env.observe(side)
        lines = describe_events(list(events), before, after, side, PLAIN)
        if lines:
            write(f"\n-- {title_for(before, after)} --\n")
            write("".join(f"  {line}\n" for line in lines))
        before = after

    tell(env.opening_events)
    commentators = (Commentator(bots[0], names[0], write), Commentator(bots[1], names[1], write))
    return play_battle(env, *commentators, tell)


class Commentator:
    """Plays as the bot it wraps, and first writes what it saw and what it chose."""

    def __init__(self, bot: Bot, name: str, write: Write) -> None:
        self._bot = bot
        self._name = name
        self._write = write

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        choice = self._bot.choose_action(side, view, legal)
        lines = [f"\n{self._name}: {_matchup_line(side, view)}", _threat_line(side, view)]
        lines += [_option_line(action, action == choice, side, view) for action in legal]
        self._write("\n".join(lines) + "\n")
        return choice

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        choice = self._bot.choose_replacement(side, view, options)
        foe = view.active(1 - side)
        lines = [f"\n{self._name}: sends a Pokemon in against {_name_hp(foe)}"]
        lines += [_replacement_line(index, index == choice, side, view) for index in options]
        self._write("\n".join(lines) + "\n")
        return choice


def _name_hp(pokemon: _engine.BattlePokemon) -> str:
    name = _engine.species_display_name(pokemon.species_id)
    return f"{name} {pokemon.current_hp}/{pokemon.stats.hp} HP"


def _matchup_line(side: int, view: _engine.BattleState) -> str:
    return f"{_name_hp(view.active(side))} against {_name_hp(view.active(1 - side))}"


def _threat_line(side: int, view: _engine.BattleState) -> str:
    active, foe = view.active(side), view.active(1 - side)
    first = "the foe" if estimate.effective_speed(foe) > estimate.effective_speed(active) else "it"
    hit = estimate.strongest_hit(foe, active)
    return f"  {first} moves first; the foe's best hit: ~{hit:.0f} HP of its {active.current_hp}"


def _option_line(action: _engine.Action, chosen: bool, side: int, view: _engine.BattleState) -> str:
    mark = CHOSEN if chosen else " "
    active, foe = view.active(side), view.active(1 - side)
    if action.kind == _engine.ACTION_MOVE:
        label = describe_move(action, view, side, hints=True)
        damage = estimate.estimate_damage(active, foe, action.index)
        knock_out = " KO" if damage >= foe.current_hp else ""
        guess = f"~{damage:.0f} HP of its {foe.current_hp}{knock_out}"
    else:
        label = describe_switch(action.index, view, side, hints=True)
        incoming = view.pokemon(side, action.index)
        taken = estimate.strongest_hit(foe, incoming)
        guess = f"takes ~{taken:.0f} HP of its {incoming.current_hp}"
    return f"  {mark} {label}  ->  {guess}"


def _replacement_line(index: int, chosen: bool, side: int, view: _engine.BattleState) -> str:
    mark = CHOSEN if chosen else " "
    incoming, foe = view.pokemon(side, index), view.active(1 - side)
    dealt = estimate.strongest_hit(incoming, foe)
    taken = estimate.strongest_hit(foe, incoming)
    label = describe_switch(index, view, side, hints=True)
    guess = f"deals ~{dealt:.0f}, takes ~{taken:.0f} HP of its {incoming.current_hp}"
    return f"  {mark} {label}  ->  {guess}"
