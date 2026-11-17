"""`trainerlab`: one command for everything.

    trainerlab                                        # a menu asks for each option
    trainerlab duel calculator survivor --pairs 5000 --from 30000

With no argument, a menu asks for the mode, then for its options one at a time, each with a
default that Enter keeps. Before running, it prints the same command in one line, to copy into a
script or a README: both forms run the same code.
"""

import argparse
import shlex
import sys
from collections.abc import Callable
from pathlib import Path
from typing import NamedTuple

from trainerlab import audit, dataset, duel, play, train, watch
from trainerlab.human import Ask
from trainerlab.options import count, seed
from trainerlab.roster import BOTS, DEFAULT_OPPONENT, DESCRIPTIONS, MODEL_BOTS
from trainerlab.screen import SPEEDS

Write = Callable[[str], None]


class Command(NamedTuple):
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
    "bots": Command("list the bots and what each one does", _list_bots),
}


def _stdout(text: str) -> None:
    sys.stdout.write(text)
    sys.stdout.flush()


def main(argv: list[str] | None = None, ask: Ask = input, write: Write = _stdout) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if not argv:
        try:
            argv = menu(ask, write)
        except (EOFError, KeyboardInterrupt):
            write("\n")
            return 130
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


# ---------------------------------------------------------------------------------------------
# The menu: each mode asks for its options and returns the arguments of its command.


def menu(ask: Ask, write: Write) -> list[str]:
    modes = [(name, command.summary) for name, command in COMMANDS.items() if name != "bots"]
    mode = _choose(ask, write, "TrainerLab", modes)
    return [mode, *QUESTIONS[mode](ask, write)]


def _play(ask: Ask, write: Write) -> list[str]:
    against = _bot(ask, write, "Opponent", DEFAULT_OPPONENT)
    seed = _text(ask, write, "Seed, to replay a battle (empty: random)", "", _is_int, empty=True)
    team = _text(ask, write, "Your team, comma-separated (empty: random)", "", None, empty=True)
    speed = _choose(ask, write, "Text speed", [(s, "") for s in SPEEDS], "normal")
    args = [] if against == DEFAULT_OPPONENT else ["--against", against]
    args += ["--seed", seed] if seed else []
    args += ["--team", team] if team else []
    return args + ([] if speed == "normal" else ["--speed", speed])


def _duel(ask: Ask, write: Write) -> list[str]:
    return [*_two_bots(ask, write), *_pairs(ask, write, 500)]


def _watch(ask: Ask, write: Write) -> list[str]:
    bots = _two_bots(ask, write)
    modes = [("losses", "list the battles bot A lost"), ("seed", "replay one battle")]
    if _choose(ask, write, "Watch", modes, "losses") == "losses":
        return [*bots, "--losses", *_pairs(ask, write, 50)]
    seed = _text(ask, write, "Seed of the pair", None, _is_int)
    side = _text(ask, write, "Bot A's side, 0 or 1", "0", lambda text: text in ("0", "1"))
    return [*bots, "--seed", seed, *([] if side == "0" else ["--side", side])]


def _audit(ask: Ask, write: Write) -> list[str]:
    return [*_two_bots(ask, write), *_pairs(ask, write, 500)]


def _dataset(ask: Ask, write: Write) -> list[str]:
    bots = _two_bots(ask, write)
    pairs, first = _how_many(ask, write, 500)
    default = dataset.default_name(bots[0], bots[1], range(first, first + pairs))
    out = _text(ask, write, "File to write", default, None)
    return [*bots, *_pair_args(pairs, first, 500), *([] if out == default else ["--out", out])]


def _train(ask: Ask, write: Write) -> list[str]:
    paths = _text(
        ask,
        write,
        "Datasets (.npz), separated by spaces",
        None,
        lambda text: all(Path(p).is_file() for p in text.split()),
    ).split()
    model = _choose(
        ask,
        write,
        "Model",
        list(train.DESCRIPTIONS.items()),
        "logistic",
    )
    default = str(train.default_name([Path(p) for p in paths], model))
    out = _text(ask, write, "File to write", default, None)
    args = paths + ([] if model == "logistic" else ["--model", model])
    return args + ([] if out == default else ["--out", out])


QUESTIONS: dict[str, Callable[[Ask, Write], list[str]]] = {
    "play": _play,
    "duel": _duel,
    "watch": _watch,
    "audit": _audit,
    "dataset": _dataset,
    "train": _train,
}


def _bot_options() -> list[tuple[str, str]]:
    return [(name, DESCRIPTIONS[name]) for name in BOTS]


def _bot(ask: Ask, write: Write, title: str, default: str | None = None) -> str:
    """A bot from the list; for the forecaster, the model it plays with too."""
    name = _choose(ask, write, title, _bot_options(), default)
    if name not in MODEL_BOTS:
        return name
    question = "Its model (empty: the one shipped)"
    model = _text(ask, write, question, "", lambda text: Path(text).is_file(), empty=True)
    return f"{name}:{model}" if model else name


def _two_bots(ask: Ask, write: Write) -> list[str]:
    bot_a = _bot(ask, write, "Bot A, the one measured")
    return [bot_a, _bot(ask, write, "Bot B, its opponent")]


def _how_many(ask: Ask, write: Write, default: int) -> tuple[int, int]:
    """How many pairs of battles, and the seed of the first one."""
    question = "How many pairs of battles (two battles each, sides swapped)"
    pairs = _text(ask, write, question, str(default), lambda text: _valid(count, text))
    first = _text(ask, write, "Seed of the first pair", "0", lambda text: _valid(seed, text))
    return int(pairs), int(first)


def _pair_args(pairs: int, first: int, default: int) -> list[str]:
    args = [] if pairs == default else ["--pairs", str(pairs)]
    return args + ([] if first == 0 else ["--from", str(first)])


def _pairs(ask: Ask, write: Write, default: int) -> list[str]:
    return _pair_args(*_how_many(ask, write, default), default)


def _is_int(text: str) -> bool:
    return text.lstrip("-").isdigit()


def _valid(parse: Callable[[str], int], text: str) -> bool:
    try:
        parse(text)
    except argparse.ArgumentTypeError:
        return False
    return True


def _choose(
    ask: Ask, write: Write, title: str, options: list[tuple[str, str]], default: str | None = None
) -> str:
    """A numbered list; the answer is a number or a name, and Enter keeps the default."""
    width = max(len(name) for name, _ in options)
    write(f"\n{title}\n")
    for number, (name, description) in enumerate(options, start=1):
        write(f"  {number}. {name:<{width}}  {description}".rstrip() + "\n")
    names = [name for name, _ in options]
    while True:
        answer = ask(f"[{default}] > " if default else "> ").strip()
        if not answer and default:
            return default
        if answer in names:
            return answer
        if answer.isdigit() and 1 <= int(answer) <= len(options):
            return names[int(answer) - 1]
        write(f"Type a number from 1 to {len(options)}.\n")


def _text(
    ask: Ask,
    write: Write,
    question: str,
    default: str | None,
    valid: Callable[[str], bool] | None,
    *,
    empty: bool = False,
) -> str:
    """A free answer; Enter keeps the default, and an invalid one is asked again."""
    shown = f" [{default}]" if default else ""
    while True:
        answer = ask(f"\n{question}{shown} > ").strip()
        if not answer and (default or empty):
            return default or ""
        if answer and (valid is None or valid(answer)):
            return answer
        write("Invalid answer, try again.\n")
