"""`trainerlab`: one command for everything.

    trainerlab                                        # the menu
    trainerlab duel calculator survivor --pairs 5000 --from 30000
    trainerlab --lang fr play

With no argument, the menu asks for the mode, then for its options one at a time, each with a
default; once the command is done, it shows the same command in one line, to copy into a
script, and comes back. Both forms run the same code. The language chosen at the first launch
is kept (~/.config/trainerlab/config.toml); --lang overrides it.
"""

import argparse
import shlex
import sys
from collections.abc import Callable
from typing import NamedTuple

from rich.console import Console

from trainerlab import i18n
from trainerlab.bots.roster import RULES
from trainerlab.commands import (
    alphazero,
    audit,
    bench,
    dataset,
    distill,
    duel,
    imitate,
    judge,
    play,
    pretrain,
    selfplay,
    teams,
    train,
    usage,
    verify,
    watch,
)
from trainerlab.commands.menu import choose_language, menu
from trainerlab.i18n import settings, t
from trainerlab.i18n.messages import MESSAGES
from trainerlab.terminal import reports
from trainerlab.terminal.prompts import Prompter, QuestionaryPrompter

INTERRUPTED = 130  # the exit status of a program stopped by Ctrl-C


class Context(NamedTuple):
    """What a command may use besides its arguments: how to ask, where to show, from where."""

    prompter: Prompter
    console: Console
    in_menu: bool


Run = Callable[[list[str], Context], int]


def _list_bots(argv: list[str], context: Context) -> int:
    context.console.print(reports.bots_table(RULES, best="alphazero"))
    return 0


def _plain(main: Callable[[list[str]], int]) -> Run:
    return lambda argv, context: main(argv)


# Every command, by section of the help, in order.
SECTIONS: dict[str, dict[str, Run]] = {
    "play": {
        "play": lambda a, c: play.main(
            a, prompter=c.prompter, console=c.console, in_menu=c.in_menu
        ),
        "watch": lambda a, c: watch.main(a, prompter=c.prompter, console=c.console),
    },
    "measure": {
        "duel": lambda a, c: duel.main(a, console=c.console),
        "usage": lambda a, c: usage.main(a, console=c.console),
        "audit": lambda a, c: audit.main(a, console=c.console),
        "bots": _list_bots,
    },
    "analyse": {
        "judge": _plain(judge.main),
        "verify": _plain(verify.main),
        "teams": _plain(teams.main),
        "bench": _plain(bench.main),
    },
    "train": {"alphazero": _plain(alphazero.main), "distill": _plain(distill.main)},
    "history": {
        "dataset": _plain(dataset.main),
        "train": _plain(train.main),
        "imitate": _plain(imitate.main),
        "pretrain": _plain(pretrain.main),
        "selfplay": _plain(selfplay.main),
    },
}
COMMANDS: dict[str, Run] = {
    name: run for section in SECTIONS.values() for name, run in section.items()
}


def translate_argparse() -> None:
    """argparse's own words (usage, options...) in the language chosen."""

    def translated(text: str) -> str:
        key = f"argparse.{text}"
        return t(key) if key in MESSAGES else text

    argparse._ = translated  # type: ignore[attr-defined]  # argparse looks its words up through its global `_`


def _language(argv: list[str]) -> list[str]:
    """Takes --lang out of the arguments and sets the language: --lang, else the one kept."""
    if "--lang" in argv:
        at = argv.index("--lang")
        if at + 1 >= len(argv) or argv[at + 1] not in i18n.LANGUAGES:
            raise SystemExit(f"trainerlab: --lang takes {' or '.join(i18n.LANGUAGES)}")
        i18n.set_language(argv[at + 1])
        return argv[:at] + argv[at + 2 :]
    i18n.set_language(settings.saved_language() or "en")
    return argv


def main(
    argv: list[str] | None = None,
    prompter: Prompter | None = None,
    console: Console | None = None,
) -> int:
    asked = list(sys.argv[1:] if argv is None else argv)
    chosen_language = "--lang" in asked
    rest = _language(asked)
    translate_argparse()
    prompter = prompter or QuestionaryPrompter()
    console = console or Console(highlight=False)
    if rest:
        return _run(rest, Context(prompter, console, in_menu=False))
    try:
        if not chosen_language and settings.saved_language() is None:
            choose_language(prompter)
        while (command := menu(prompter, console)) is not None:
            if command:
                _run(command, Context(prompter, console, in_menu=True))
                shown = shlex.join(["trainerlab", *command])
                console.print(f"[dim]{t('menu.command', command=shown)}[/]", highlight=False)
    except (EOFError, KeyboardInterrupt):
        console.print()
        return INTERRUPTED
    return 0


def _run(argv: list[str], context: Context) -> int:
    command, rest = argv[0], argv[1:]
    if command in ("-h", "--help"):
        context.console.print(usage_text(), highlight=False)
        return 0
    if command not in COMMANDS:
        context.console.print(t("help.unknown", command=command) + "\n", markup=False)
        context.console.print(usage_text(), highlight=False)
        return 2
    return COMMANDS[command](rest, context)


def usage_text() -> str:
    colon = " : " if i18n.language() == "fr" else ": "
    tagline = t("menu.tagline")
    lines = [f"[bold]TrainerLab[/]{colon}{tagline}", "", t("help.usage"), t("help.menu")]
    for section, commands in SECTIONS.items():
        lines += ["", f" [bold yellow]{t(f'help.section.{section}')}[/]"]
        lines += [f"   [cyan]{name:<10}[/] {t(f'command.{name}')}" for name in commands]
    lines += ["", t("help.lang"), t("help.more")]
    return "\n".join(lines)
