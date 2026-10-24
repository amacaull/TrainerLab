"""Command line entry point: replays a battle between two bots, with what each bot saw.

    trainerlab-watch survival matchup --losses --pairs 50 --first-seed 20000
    trainerlab-watch survival matchup --seed 20017 --a-side 1

The first form lists the battles of a tournament that bot A lost, as commands to watch each one.
The second replays one of them: at every decision, the options the bot had, its estimate of
each one and the one it chose; then what really happened. An error stands out where the
estimate and the events disagree.
"""

import argparse
import signal
import sys
from collections.abc import Callable

from trainerlab import _engine, estimate
from trainerlab.battle import play_battle
from trainerlab.bots import Bot
from trainerlab.environment import random_team_pair
from trainerlab.harness import outcome_of, pair_battles, play_pair
from trainerlab.play import title_for
from trainerlab.render import Style, describe_events, describe_move, describe_switch
from trainerlab.tournament import BOTS

Write = Callable[[str], None]

CHOSEN = ">"
PLAIN = Style(enabled=False)
RESULTS = {"win": "won", "draw": "drew", "loss": "lost"}


def _stdout(text: str) -> None:
    sys.stdout.write(text)


def main(argv: list[str] | None = None, write: Write = _stdout) -> int:
    parser = argparse.ArgumentParser(description="Replay a battle between two bots.")
    parser.add_argument("bot_a", choices=sorted(BOTS))
    parser.add_argument("bot_b", choices=sorted(BOTS))
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--seed", type=int, help="the pair of the battle to replay")
    mode.add_argument("--losses", action="store_true", help="list the battles bot A lost")
    parser.add_argument("--a-side", type=int, choices=(0, 1), default=0, help="bot A's side")
    parser.add_argument("--pairs", type=int, default=50, help="pairs to scan with --losses")
    parser.add_argument("--first-seed", type=int, default=0, help="first seed with --losses")
    args = parser.parse_args(argv)
    if write is _stdout:
        # Stop quietly when the output is piped into a command that exits early (head, grep -q).
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)

    if args.losses:
        _list_losses(args.bot_a, args.bot_b, args.first_seed, args.pairs, write)
    else:
        _watch(args.bot_a, args.bot_b, args.seed, args.a_side, write)
    return 0


def _list_losses(bot_a: str, bot_b: str, first_seed: int, pairs: int, write: Write) -> None:
    lost = 0
    for seed in range(first_seed, first_seed + pairs):
        team0, team1 = random_team_pair(seed)
        outcomes = play_pair(BOTS[bot_a], BOTS[bot_b], seed=seed, team0=team0, team1=team1)
        for a_side, outcome in enumerate(outcomes):
            if outcome == "loss":
                lost += 1
                write(f"trainerlab-watch {bot_a} {bot_b} --seed {seed} --a-side {a_side}\n")
    write(f"{bot_a} lost {lost} of {2 * pairs} battles against {bot_b}.\n")


def _watch(bot_a: str, bot_b: str, seed: int, a_side: int, write: Write) -> None:
    team0, team1 = random_team_pair(seed)
    battles = pair_battles(BOTS[bot_a], BOTS[bot_b], seed=seed, team0=team0, team1=team1)
    _, env, bot0, bot1 = next(battle for battle in battles if battle[0] == a_side)
    names = {a_side: f"A, {bot_a}", 1 - a_side: f"B, {bot_b}"}
    teams = {a_side: team0 if a_side == 0 else team1, 1 - a_side: team1 if a_side == 0 else team0}
    write(f"Seed {seed}. Bot A = {bot_a}, side {a_side}. Bot B = {bot_b}, side {1 - a_side}.\n")
    write("A's team: " + ", ".join(teams[a_side]) + "\n")
    write("B's team: " + ", ".join(teams[1 - a_side]) + "\n")
    write("In the events, 'your' is bot A and 'the foe' is bot B.\n")

    before = env.observe(a_side)

    def tell(events: list[_engine.Event]) -> None:
        nonlocal before
        after = env.observe(a_side)
        lines = describe_events(list(events), before, after, a_side, PLAIN)
        if lines:
            write(f"\n-- {title_for(before, after)} --\n")
            write("".join(f"  {line}\n" for line in lines))
        before = after

    tell(env.opening_events)
    bots = (_Commentator(bot0, names[0], write), _Commentator(bot1, names[1], write))
    winner = play_battle(env, *bots, tell)
    result = RESULTS[outcome_of(winner, a_side)]
    write(f"\nBot A ({bot_a}) {result} in {env.turn} turns.\n")


class _Commentator:
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
