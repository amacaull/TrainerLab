"""`trainerlab`: one command for everything.

    trainerlab                                        # a menu asks for each option
    trainerlab duel calculator survivor --pairs 5000 --from 30000

With no argument, a menu asks for the mode, then for its options one at a time, each with a
default that Enter keeps. Before running, it prints the same command in one line, to copy into a
script or a README: both forms run the same code.
"""

import shlex
import sys
from collections.abc import Callable
from typing import NamedTuple

from trainerlab.bots.roster import BOTS, DESCRIPTIONS
from trainerlab.commands import audit, bench, dataset, duel, play, train, usage, watch
from trainerlab.commands.console import stdout
from trainerlab.commands.menu import menu
from trainerlab.terminal.human import Ask
from trainerlab.terminal.screen import Write

INTERRUPTED = 130  # the exit status of a program stopped by Ctrl-C


class Command(NamedTuple):
    """A subcommand: its line in the help, and how to run it."""

    summary: str
    run: Callable[[list[str], Ask], int]


def _list_bots(argv: list[str], ask: Ask) -> int:
    width = max(len(name) for name in BOTS)
    for name in BOTS:
        print(f"{name:<{width}}  {DESCRIPTIONS[name]}")
    return 0


COMMANDS: dict[str, Command] = {
    "play": Command("play a battle against a bot", lambda a, ask: play.main(a, ask=ask)),
    "duel": Command("score one bot against another over paired battles", lambda a, _: duel.main(a)),
    "watch": Command("replay a battle between two bots", lambda a, _: watch.main(a)),
    "audit": Command("count the mistakes a program can spot in a bot", lambda a, _: audit.main(a)),
    "dataset": Command("turn battles into a dataset to learn from", lambda a, _: dataset.main(a)),
    "train": Command("fit a win probability model on a dataset", lambda a, _: train.main(a)),
    "usage": Command("count the kinds of moves a bot plays", lambda a, _: usage.main(a)),
    "bots": Command("list the bots and what each one does", _list_bots),
    "bench": Command("measure how fast the bots play", lambda a, _: bench.main(a)),
}


# The modes of the menu: every command but the list of bots and the benchmark.
MODES = [(name, c.summary) for name, c in COMMANDS.items() if name not in ("bots", "bench")]


def main(argv: list[str] | None = None, ask: Ask = input, write: Write = stdout) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if not argv:
        try:
            argv = menu(ask, write, MODES)
        except (EOFError, KeyboardInterrupt):
            write("\n")
            return INTERRUPTED
        write(f"\n{shlex.join(['trainerlab', *argv])}\n\n")
    command, rest = argv[0], argv[1:]
    if command in ("-h", "--help"):
        write(_usage())
        return 0
    if command not in COMMANDS:
        write(f"trainerlab: unknown command {command!r}\n\n{_usage()}")
        return 2
    return COMMANDS[command].run(rest, ask)


def _usage() -> str:
    lines = ["usage: trainerlab [COMMAND [OPTIONS]]", "", "With no command, a menu asks for all."]
    lines += [f"  {name:<8} {command.summary}" for name, command in COMMANDS.items()]
    lines += ["", "trainerlab COMMAND --help shows the options of a command."]
    return "\n".join(lines) + "\n"
