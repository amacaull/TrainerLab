"""`trainerlab play`: play a battle against a bot from the terminal.

trainerlab play --level hard --seed 42
trainerlab play --against strategist
trainerlab play --team Dragapult,Snorlax,Toxapex,Excadrill,Blissey,Corviknight
"""

import argparse
import random
import re
import sys
import unicodedata
from contextlib import ExitStack
from datetime import datetime
from pathlib import Path

from rich.console import Console

from trainerlab import _engine
from trainerlab.bots.levels import DEFAULT_LEVEL, LEVELS, level_spec
from trainerlab.bots.roster import LEAD, WITH_MODEL, bot_spec, factory, searcher
from trainerlab.game.building import bot_team
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.game.pairs import led
from trainerlab.i18n import names, settings, t
from trainerlab.terminal.human import Fled
from trainerlab.terminal.prompts import Option, Prompter, QuestionaryPrompter
from trainerlab.terminal.render import Style, species_card, species_label
from trainerlab.terminal.screen import SPEEDS, Screen
from trainerlab.terminal.session import play_session

ABANDONED = 130  # the exit status of a program stopped by Ctrl-C
REMATCH, SAME, SAVE, LEAVE = "rematch", "same", "save", "leave"


def new_seed() -> int:
    return random.SystemRandom().randrange(1_000_000)


def main(
    argv: list[str] | None = None,
    *,
    prompter: Prompter | None = None,
    console: Console | None = None,
    in_menu: bool = False,
) -> int:
    parser = argparse.ArgumentParser(prog="trainerlab play", description=t("command.play"))
    who = parser.add_mutually_exclusive_group()
    who.add_argument("--level", type=level_spec, help=t("opt.level"))
    who.add_argument(
        "--against",
        type=bot_spec,
        help=t("opt.against", model=WITH_MODEL, default=LEVELS[DEFAULT_LEVEL][0]),
    )
    parser.add_argument("--seed", type=int, help=t("opt.play_seed"))
    parser.add_argument("--team", help=t("opt.team"))
    parser.add_argument("--size", type=int, choices=range(1, 7), default=6, help=t("opt.size"))
    parser.add_argument("--speed", choices=list(SPEEDS), default="normal", help=t("menu.speed"))
    parser.add_argument("--log", help=t("opt.log"))
    parser.add_argument("--no-color", action="store_true", help=t("opt.no_color"))
    args = parser.parse_args(argv)

    asker = prompter or QuestionaryPrompter()
    terminal = sys.stdout.isatty()
    console = console or Console(highlight=False)
    style = Style(enabled=console.is_terminal and not args.no_color)
    seed = args.seed if args.seed is not None else new_seed()
    chosen = (
        [names.species_key(name) or name for name in args.team.split(",")] if args.team else None
    )

    # A level names its bot and the kind of its team; a bot named builds its team as species.
    if args.against is not None:
        bot, species, label = args.against, True, args.against
    else:
        level = args.level or DEFAULT_LEVEL
        (bot, species), label = LEVELS[level], t(f"level.{level}")
    # A search bot chooses its lead, as a player does (palier 13.1); measures leave it out.
    playing = bot + LEAD if searcher(bot) else bot

    with ExitStack() as files:
        log = None
        if args.log:
            log_file = files.enter_context(open(args.log, "w"))
            log = Console(file=log_file, color_system=None, width=console.width, highlight=False)
        screen = Screen(console, speed=args.speed, clear=terminal, log=log)
        while True:
            size = len(chosen) if chosen else args.size
            mine, foe_team = random_team_pair(seed, size)
            mine = chosen or mine
            foe_team = bot_team(seed, size) if species else foe_team
            try:
                BattleEnvironment(mine, foe_team, seed=seed)  # the teams checked before asking
            except (ValueError, _engine.TeamError) as error:
                screen.message(t("play.invalid_team", error=error))
                return 2
            trainer = names.leader(seed)
            try:
                mine = lead_first(asker, mine)
                opponent = factory(playing)(seed)
                env = BattleEnvironment(mine, led(opponent, foe_team), seed=seed)
                screen.start_transcript()
                try:
                    play_session(
                        env,
                        opponent,
                        label,
                        trainer,
                        seed,
                        screen,
                        style,
                        asker,
                        level=args.against is None,
                    )
                except Fled:
                    screen.panel(
                        style.bold(t("play.defeat")),
                        t("play.fled", trainer=trainer, bot=label),
                    )
                leave = t("play.menu") if in_menu else t("play.quit")
                options = [
                    Option(REMATCH, t("play.rematch")),
                    Option(SAME, t("play.same")),
                    Option(SAVE, t("play.save")),
                    Option(LEAVE, leave),
                ]
                while (again := asker.select(t("play.again"), options)) == SAVE:
                    path = save(screen.transcript(), trainer, label, seed)
                    screen.message(t("play.saved", path=path))
            except (EOFError, KeyboardInterrupt):
                screen.message(t("play.abandoned"))
                return ABANDONED
            if again == LEAVE:
                return 0
            seed = new_seed() if again == REMATCH else seed


def lead_first(prompter: Prompter, team: list[str]) -> list[str]:
    """The team, the Pokemon the player sends in first at its head."""
    if len(team) == 1:
        return team
    options = [
        Option(index, species_label(key), species_card(key)) for index, key in enumerate(team)
    ]
    lead = int(prompter.select(t("play.lead"), options))
    return [team[lead], *team[:lead], *team[lead + 1 :]]


def slug(text: str) -> str:
    """A name fit for a file: 'Lt. Surge' -> 'lt-surge', 'Érika' -> 'erika'."""
    ascii_text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", ascii_text.lower()).strip("-")


def save(transcript: str, trainer: str, bot: str, seed: int) -> Path:
    """The battle just played, as shown, in a file of its own in the games folder."""
    folder = settings.games_dir()
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{datetime.now():%Y-%m-%d_%H%M}_{slug(trainer)}-{slug(bot)}_{seed}.txt"
    path.write_text(transcript)
    return path
