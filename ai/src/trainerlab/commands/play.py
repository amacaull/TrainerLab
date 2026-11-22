"""`trainerlab play`: play a battle against a bot from the terminal.

trainerlab play --against strategist --seed 42
trainerlab play --team Dragapult,Snorlax,Toxapex,Excadrill,Blissey,Corviknight
"""

import argparse
import random
import sys
from contextlib import ExitStack

from rich.console import Console

from trainerlab import _engine
from trainerlab.bots.roster import DEFAULT_OPPONENT, WITH_MODEL, bot_spec, factory
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.i18n import names, t
from trainerlab.terminal.human import Fled
from trainerlab.terminal.prompts import Option, Prompter, QuestionaryPrompter
from trainerlab.terminal.render import Style
from trainerlab.terminal.screen import SPEEDS, Screen
from trainerlab.terminal.session import play_session

ABANDONED = 130  # the exit status of a program stopped by Ctrl-C
REMATCH, SAME, LEAVE = "rematch", "same", "leave"


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
    parser.add_argument(
        "--against",
        type=bot_spec,
        default=DEFAULT_OPPONENT,
        help=t("opt.against", model=WITH_MODEL, default=DEFAULT_OPPONENT),
    )
    parser.add_argument("--seed", type=int, help=t("opt.play_seed"))
    parser.add_argument("--team", help=t("opt.team"))
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

    with ExitStack() as files:
        log = None
        if args.log:
            log_file = files.enter_context(open(args.log, "w"))
            log = Console(file=log_file, color_system=None, width=console.width, highlight=False)
        screen = Screen(console, speed=args.speed, clear=terminal, log=log)
        while True:
            team, foe_team = random_team_pair(seed)
            try:
                env = BattleEnvironment(chosen or team, foe_team, seed=seed)
            except (ValueError, _engine.TeamError) as error:
                screen.message(t("play.invalid_team", error=error))
                return 2
            opponent = factory(args.against)(seed)
            trainer = names.leader(seed)
            try:
                try:
                    play_session(env, opponent, args.against, trainer, seed, screen, style, asker)
                except Fled:
                    screen.panel(
                        style.bold(t("play.defeat")),
                        t("play.fled", trainer=trainer, bot=args.against),
                    )
                leave = t("play.menu") if in_menu else t("play.quit")
                again = asker.select(
                    t("play.again"),
                    [
                        Option(REMATCH, t("play.rematch")),
                        Option(SAME, t("play.same")),
                        Option(LEAVE, leave),
                    ],
                )
            except (EOFError, KeyboardInterrupt):
                screen.message(t("play.abandoned"))
                return ABANDONED
            if again == LEAVE:
                return 0
            seed = new_seed() if again == REMATCH else seed
