"""`trainerlab play`: play a battle against a bot from the terminal.

trainerlab play --against strategist --seed 42
trainerlab play --team Dragapult,Snorlax,Toxapex,Excadrill,Blissey,Corviknight --no-hints
"""

import argparse
import random
import shutil
import sys
from contextlib import ExitStack

from trainerlab import _engine
from trainerlab.bots.roster import DEFAULT_OPPONENT, WITH_MODEL, bot_spec, factory
from trainerlab.commands.console import stdout
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.terminal.human import Ask
from trainerlab.terminal.render import Style
from trainerlab.terminal.screen import SPEEDS, Screen, Write
from trainerlab.terminal.session import play_session


def main(argv: list[str] | None = None, ask: Ask = input, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab play", description="Play a battle against a bot."
    )
    parser.add_argument(
        "--against",
        type=bot_spec,
        default=DEFAULT_OPPONENT,
        help=f"the bot to play against, or {WITH_MODEL}:MODEL.npz (default: {DEFAULT_OPPONENT})",
    )
    parser.add_argument("--seed", type=int, help="replays the same battle (default: random)")
    parser.add_argument("--team", help="your Pokemon, comma-separated (default: random)")
    parser.add_argument("--speed", choices=list(SPEEDS), default="normal", help="text speed")
    parser.add_argument(
        "--no-hints", action="store_true", help="hide the type effectiveness of moves"
    )
    parser.add_argument("--log", help="also write the battle, without colours, to this file")
    parser.add_argument("--no-color", action="store_true", help="plain text output")
    args = parser.parse_args(argv)

    seed = args.seed if args.seed is not None else random.SystemRandom().randrange(1_000_000)
    terminal = sys.stdout.isatty()
    style = Style(enabled=terminal and not args.no_color)
    random_team, foe_team = random_team_pair(seed)
    team = args.team.split(",") if args.team else random_team
    try:
        env = BattleEnvironment(team, foe_team, seed=seed)
    except (ValueError, _engine.TeamError) as error:
        write(f"Invalid team: {error}\n")
        return 2

    with ExitStack() as files:
        log = None
        if args.log:
            log_file = files.enter_context(open(args.log, "w"))

            def log(line: str) -> None:
                log_file.write(line + "\n")

        width = max(60, min(100, shutil.get_terminal_size().columns))
        screen = Screen(write, width=width, speed=args.speed, clear=terminal, log=log)
        opponent = factory(args.against)(seed)
        return play_session(
            env, opponent, args.against, seed, team, screen, style, ask, not args.no_hints
        )
