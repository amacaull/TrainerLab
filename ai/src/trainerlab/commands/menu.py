"""The menu of `trainerlab`: a mode, then its options one at a time, each with a default.

Each mode asks its questions and returns the arguments of its command, as typed in one line;
the menu comes back once the command is done.
"""

import argparse
from collections.abc import Callable

from rich.console import Console, Group
from rich.panel import Panel
from rich.text import Text

from trainerlab.bots.roster import BOTS, DEFAULT_OPPONENT, FEATURED
from trainerlab.commands.options import PAIRS, SCANNED_PAIRS, count, seed
from trainerlab.commands.usage import PAIRS as USAGE_PAIRS
from trainerlab.i18n import LANGUAGES, language, names, set_language, settings, t
from trainerlab.terminal.prompts import Label, Line, Option, Prompter, Separator
from trainerlab.terminal.screen import SPEEDS

OTHER = "other"  # the entry that opens the list of every bot
BACK = "back"  # the last entry of every question's list
LANGUAGE, QUIT = "language", "quit"
DEFAULT_A = "alphazero"  # the bot measured, unless another is chosen
LANGUAGE_NAMES = {"fr": "Français", "en": "English"}


def choose_language(prompter: Prompter) -> None:
    """At the first launch: the language, kept for the next ones."""
    options = [Option(code, LANGUAGE_NAMES[code]) for code in ("fr", "en")]
    chosen = prompter.select(t("menu.language"), options, default="fr")
    set_language(chosen)
    settings.save_language(chosen)


def header() -> Panel:
    title = Text("TrainerLab", style="bold yellow", justify="center")
    tagline = Text(t("menu.tagline"), justify="center")
    return Panel(Group(title, tagline), border_style="dim", padding=(0, 4), expand=False)


def menu(prompter: Prompter, console: Console) -> list[str] | None:
    """The command chosen and its options; None to quit; [] when only the language changed."""
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
    mode = prompter.select(t("menu.what"), options, default="play")
    if mode == QUIT:
        return None
    if mode == LANGUAGE:
        other = LANGUAGES[1 - LANGUAGES.index(language())]
        set_language(other)
        settings.save_language(other)
        return []
    try:
        return [mode, *QUESTIONS[mode](prompter)]
    except (Back, KeyboardInterrupt):  # Back, or Ctrl-C, in a question: the main menu again
        return []


class Back(Exception):  # noqa: N818 (a way out, not an error)
    """The player chose ← Back in a question."""


def choose(prompter: Prompter, message: str, options: list[Line], default: object = None) -> object:
    """A question's list, with ← Back at its end: Back raises Back."""
    chosen = prompter.select(message, [*options, Separator(), Option(BACK, t("back"))], default)
    if chosen == BACK:
        raise Back
    return chosen


def _bot_label(name: str, described: bool) -> Label:
    if not described:
        return name
    return [("bold", f"{name:<12}"), ("fg:ansibrightblack", t(f"short.{name}"))]


def bot(prompter: Prompter, message: str, default: str, *, described: bool = True) -> str:
    """One of the featured bots, or any bot from the whole list, filtered as one types."""
    options: list[Line] = [Option(name, _bot_label(name, described)) for name in FEATURED]
    other: Label = (
        [("bold", f"{t('menu.other_bot'):<12}"), ("fg:ansibrightblack", t("menu.other_bot_card"))]
        if described
        else t("menu.other_bot")
    )
    options += [Separator(), Option(OTHER, other)]
    every: list[Line] = [
        Option(name, _bot_label(name, True)) for name in BOTS if name not in FEATURED
    ]
    every += [Separator(), Option(BACK, t("back"))]
    while True:
        chosen = str(choose(prompter, message, options, default))
        if chosen != OTHER:
            return chosen
        picked = str(prompter.select(t("menu.which_bot"), every, search=True))
        if picked != BACK:
            return picked


def _two_bots(prompter: Prompter) -> list[str]:
    return [
        bot(prompter, t("menu.bot_a"), DEFAULT_A),
        bot(prompter, t("menu.bot_b"), DEFAULT_OPPONENT),
    ]


def _valid(parse: Callable[[str], int]) -> Callable[[str], bool]:
    def check(text: str) -> bool:
        try:
            parse(text)
        except argparse.ArgumentTypeError:
            return False
        return True

    return check


def _pairs(prompter: Prompter, default: int) -> list[str]:
    """How many pairs and from which seed, leaving out what the command takes by default."""
    pairs = prompter.text(t("menu.pairs"), str(default), _valid(count))
    first = prompter.text(t("menu.first_seed"), "0", _valid(seed))
    args = [] if int(pairs) == default else ["--pairs", pairs]
    return args + ([] if int(first) == 0 else ["--from", first])


def _play(prompter: Prompter) -> list[str]:
    against = bot(prompter, t("menu.opponent"), DEFAULT_OPPONENT, described=False)
    args = [] if against == DEFAULT_OPPONENT else ["--against", against]
    team_options: list[Line] = [
        Option("random", t("menu.random_team")),
        Option("own", t("menu.own_team")),
    ]
    if choose(prompter, t("menu.team"), team_options, "random") == "own":
        everyone = names.all_species()
        picked: list[str] = []
        for number in range(1, 7):
            name = prompter.autocomplete(
                t("menu.pokemon", number=number),
                [n for n in everyone if n not in picked],
                lambda text: names.species_key(text) is not None and text not in picked,
            )
            picked.append(name)
        args += ["--team", ",".join(str(names.species_key(name)) for name in picked)]
    chosen = prompter.text(t("menu.seed"), "", lambda text: text == "" or text.isdigit())
    args += ["--seed", chosen] if chosen else []
    speeds: list[Line] = [Option(speed, t(f"speed.{speed}")) for speed in SPEEDS]
    speed = str(choose(prompter, t("menu.speed"), speeds, "normal"))
    return args + ([] if speed == "normal" else ["--speed", speed])


def _watch(prompter: Prompter) -> list[str]:
    bots = _two_bots(prompter)
    modes: list[Line] = [
        Option("losses", t("menu.watch_losses", bot=bots[0])),
        Option("seed", t("menu.watch_seed")),
    ]
    if choose(prompter, t("menu.watch_what"), modes, "losses") == "losses":
        return [*bots, "--losses", *_pairs(prompter, SCANNED_PAIRS)]
    chosen = prompter.text(t("menu.battle_seed"), "", _valid(seed))
    side = prompter.text(t("menu.side"), "0", lambda text: text in ("0", "1"))
    return [*bots, "--seed", chosen, *([] if side == "0" else ["--side", side])]


QUESTIONS: dict[str, Callable[[Prompter], list[str]]] = {
    "play": _play,
    "watch": _watch,
    "duel": lambda p: [*_two_bots(p), *_pairs(p, PAIRS)],
    "usage": lambda p: [*_two_bots(p), *_pairs(p, USAGE_PAIRS)],
    "audit": lambda p: [*_two_bots(p), *_pairs(p, PAIRS)],
    "bots": lambda p: [],
}
