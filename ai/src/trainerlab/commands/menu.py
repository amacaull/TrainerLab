"""The menu of `trainerlab`: a mode, then its questions one at a time.

Each mode is a form: questions in order, none answered in advance. ← Back (or Ctrl-C) goes back
to the question before, and from the first one to the menu. The answers become the arguments of
the command, as typed in one line; the menu comes back once the command is done.
"""

import argparse
from collections.abc import Callable
from typing import Any

from rich.console import Console, Group
from rich.panel import Panel
from rich.text import Text

from trainerlab.bots.roster import BOTS, DEFAULT_OPPONENT, FEATURED
from trainerlab.commands.options import PAIRS, SCANNED_PAIRS, count, seed
from trainerlab.commands.usage import PAIRS as USAGE_PAIRS
from trainerlab.i18n import LANGUAGES, language, names, set_language, settings, t
from trainerlab.terminal.prompts import Label, Line, Option, Prompter, Separator
from trainerlab.terminal.render import species_label

OTHER = "other"  # the entry that opens the list of every bot
BACK = "back"  # the last entry of every question's list
LANGUAGE, QUIT = "language", "quit"
LANGUAGE_NAMES = {"fr": "Français", "en": "English"}
SPEEDS = ("slow", "normal", "fast")  # the text speeds offered; instant is for the tests
SKIP = object()  # the answer of a question that does not apply (the seed, for losses)

Answers = dict[str, Any]
Step = Callable[[Prompter, Answers], Any]


class Back(Exception):  # noqa: N818 (a way out, not an error)
    """The player chose ← Back in a question."""


def choose_language(prompter: Prompter) -> None:
    """At the first launch: the language, kept for the next ones."""
    options = [Option(code, LANGUAGE_NAMES[code]) for code in ("fr", "en")]
    chosen = prompter.select(t("menu.language"), options)
    set_language(chosen)
    settings.save_language(chosen)


def header() -> Panel:
    title = Text("TrainerLab", style="bold yellow", justify="center")
    tagline = Text(t("menu.tagline"), justify="center")
    return Panel(Group(title, tagline), border_style="dim", padding=(0, 4), expand=False)


def menu(prompter: Prompter, console: Console) -> list[str] | None:
    """The command chosen and its options; None to quit; [] for the menu again (the language
    changed, or Back from the first question). Starts on a fresh screen, at its top."""
    console.clear()
    console.print(header())
    options: list[Line] = [
        Separator(t("menu.play_section")),
        Option("play", t("menu.play")),
        Option("watch", t("menu.watch")),
        Separator(t("menu.measure_section")),
        Option("duel", t("menu.duel")),
        Option("usage", t("menu.usage")),
        Option("audit", t("menu.audit")),
        Option("bots", t("menu.bots")),
        Separator(),
        Option(LANGUAGE, t("menu.switch_language")),
        Option(QUIT, t("menu.quit")),
    ]
    mode = prompter.select(t("menu.what"), options)
    if mode == QUIT:
        return None
    if mode == LANGUAGE:
        other = LANGUAGES[1 - LANGUAGES.index(language())]
        set_language(other)
        settings.save_language(other)
        return []
    steps, arguments = FORMS[mode]
    answers = form(prompter, steps)
    return [] if answers is None else [mode, *arguments(answers)]


def form(prompter: Prompter, steps: list[tuple[str, Step]]) -> Answers | None:
    """Asks the steps in order; Back (or Ctrl-C) asks the one before again. None when the player
    goes back from the first."""
    answers: Answers = {}
    at = 0
    while at < len(steps):
        key, step = steps[at]
        try:
            answers[key] = step(prompter, answers)
            at += 1
        except (Back, KeyboardInterrupt):
            at -= 1
            while at >= 0 and answers[steps[at][0]] is SKIP:
                at -= 1
            if at < 0:
                return None
            for later, _ in steps[at:]:
                answers.pop(later, None)
    return answers


def choose(prompter: Prompter, message: str, options: list[Line]) -> Any:
    """A question's list, with ← Back at its end: Back raises Back."""
    chosen = prompter.select(message, [*options, Separator(), Option(BACK, t("back"))])
    if chosen == BACK:
        raise Back
    return chosen


def _bot_label(name: str, described: bool) -> Label:
    if not described:
        return name
    return [("bold", f"{name:<12}"), ("fg:ansibrightblack", t(f"short.{name}"))]


def bot_step(message: str, *, described: bool = True) -> Step:
    """One of the featured bots, or any bot from the whole list, filtered as one types; Back
    from the whole list returns to the featured ones."""

    def ask(prompter: Prompter, answers: Answers) -> str:
        options: list[Line] = [Option(name, _bot_label(name, described)) for name in FEATURED]
        other: Label = t("menu.other_bot")
        if described:
            other = [("bold", f"{other:<12}"), ("fg:ansibrightblack", t("menu.other_bot_card"))]
        options += [Separator(), Option(OTHER, other)]
        every: list[Line] = [
            Option(name, _bot_label(name, True)) for name in BOTS if name not in FEATURED
        ]
        every += [Separator(), Option(BACK, t("back"))]
        while True:
            chosen = str(choose(prompter, t(message), options))
            if chosen != OTHER:
                return chosen
            picked = str(prompter.select(t("menu.which_bot"), every, search=True))
            if picked != BACK:
                return picked

    return ask


def _valid(parse: Callable[[str], int]) -> Callable[[str], bool]:
    def check(text: str) -> bool:
        try:
            parse(text)
        except argparse.ArgumentTypeError:
            return False
        return True

    return check


def _asked_text(message: str, valid: Callable[[str], bool], **fields: object) -> Step:
    return lambda prompter, answers: prompter.text(t(message, **fields), valid)


def _unless(skipped: Callable[[Answers], bool], step: Step) -> Step:
    """A step asked only when it applies: SKIP otherwise."""
    return lambda prompter, answers: SKIP if skipped(answers) else step(prompter, answers)


def _speed(prompter: Prompter, answers: Answers) -> str:
    return str(choose(prompter, t("menu.speed"), [Option(s, t(f"speed.{s}")) for s in SPEEDS]))


def _size(prompter: Prompter, answers: Answers) -> int:
    sizes: list[Line] = [Option(n, t("menu.size_option", size=n)) for n in range(6, 0, -1)]
    return int(choose(prompter, t("menu.size"), sizes))


def _team_mode(prompter: Prompter, answers: Answers) -> str:
    modes: list[Line] = [Option("random", t("menu.random_team")), Option("own", t("menu.own_team"))]
    return str(choose(prompter, t("menu.team"), modes))


def _builder(prompter: Prompter, answers: Answers) -> list[str]:
    """The team, built Pokemon by Pokemon from the list: add, remove, then confirm."""
    size, picked = answers["size"], list[str]()
    while True:
        team = ", ".join(names.species_name(key) for key in picked) or "—"
        actions: list[Line] = []
        if len(picked) == size:
            actions.append(Option("done", t("menu.team_done")))
        if len(picked) < size:
            actions.append(Option("add", t("menu.team_add")))
        if picked:
            actions.append(Option("remove", t("menu.team_remove")))
        message = t("menu.team_so_far", count=len(picked), size=size, team=team)
        action = choose(prompter, message, actions)
        if action == "done":
            return picked
        if action == "add":
            keys = [key for key in names.species_keys() if key not in picked]
            listed: list[Line] = [Option(key, species_label(key)) for key in keys]
            listed += [Separator(), Option(BACK, t("back"))]
            added = prompter.select(t("menu.team_pick"), listed, search=True)
        else:
            listed = [Option(key, names.species_name(key)) for key in picked]
            listed += [Separator(), Option(BACK, t("back"))]
            added = prompter.select(t("menu.team_which"), listed)
            if added != BACK:
                picked.remove(added)
            continue
        if added != BACK:
            picked.append(added)


def _side(prompter: Prompter, answers: Answers) -> str:
    sides: list[Line] = [Option("0", "0"), Option("1", "1")]
    return str(choose(prompter, t("menu.side"), sides))


def _watch_mode(prompter: Prompter, answers: Answers) -> str:
    modes: list[Line] = [
        Option("losses", t("menu.watch_losses", bot=answers["a"])),
        Option("seed", t("menu.watch_seed")),
    ]
    return str(choose(prompter, t("menu.watch_what"), modes))


def _pair_args(answers: Answers, default: int) -> list[str]:
    """--pairs and --from, left out when they are what the command takes by default."""
    pairs, first = int(answers["pairs"]), int(answers["first"])
    args = [] if pairs == default else ["--pairs", str(pairs)]
    return args + ([] if first == 0 else ["--from", str(first)])


def _measure(default: int) -> tuple[list[tuple[str, Step]], Callable[[Answers], list[str]]]:
    steps: list[tuple[str, Step]] = [
        ("a", bot_step("menu.bot_a")),
        ("b", bot_step("menu.bot_b")),
        ("pairs", _asked_text("menu.pairs", _valid(count), advised=default)),
        ("first", _asked_text("menu.first_seed", _valid(seed))),
    ]
    return steps, lambda answers: [answers["a"], answers["b"], *_pair_args(answers, default)]


def _play_args(answers: Answers) -> list[str]:
    args = [] if answers["against"] == DEFAULT_OPPONENT else ["--against", answers["against"]]
    if answers["team"] is not SKIP:
        args += ["--team", ",".join(answers["team"])]
    elif answers["size"] != 6:
        args += ["--size", str(answers["size"])]
    args += ["--seed", answers["seed"]] if answers["seed"] else []
    return args + ([] if answers["speed"] == "normal" else ["--speed", answers["speed"]])


def _watch_args(answers: Answers) -> list[str]:
    args = [answers["a"], answers["b"]]
    if answers["mode"] == "losses":
        args += ["--losses", *_pair_args(answers, SCANNED_PAIRS)]
    else:
        args += ["--seed", answers["seed"]] + ([] if answers["side"] == "0" else ["--side", "1"])
    return args + ([] if answers["speed"] == "normal" else ["--speed", answers["speed"]])


def _losses(answers: Answers) -> bool:
    return bool(answers["mode"] == "losses")


FORMS: dict[str, tuple[list[tuple[str, Step]], Callable[[Answers], list[str]]]] = {
    "play": (
        [
            ("against", bot_step("menu.opponent", described=False)),
            ("size", _size),
            ("mode", _team_mode),
            ("team", _unless(lambda answers: answers["mode"] == "random", _builder)),
            ("seed", _asked_text("menu.seed", lambda text: text == "" or text.isdigit())),
            ("speed", _speed),
        ],
        _play_args,
    ),
    "watch": (
        [
            ("a", bot_step("menu.bot_a")),
            ("b", bot_step("menu.bot_b")),
            ("mode", _watch_mode),
            (
                "pairs",
                _unless(
                    lambda a: not _losses(a),
                    _asked_text("menu.pairs", _valid(count), advised=SCANNED_PAIRS),
                ),
            ),
            (
                "first",
                _unless(lambda a: not _losses(a), _asked_text("menu.first_seed", _valid(seed))),
            ),
            ("seed", _unless(_losses, _asked_text("menu.battle_seed", _valid(seed)))),
            ("side", _unless(_losses, _side)),
            ("speed", _speed),
        ],
        _watch_args,
    ),
    "duel": _measure(PAIRS),
    "usage": _measure(USAGE_PAIRS),
    "audit": _measure(PAIRS),
    "bots": ([], lambda answers: []),
}
