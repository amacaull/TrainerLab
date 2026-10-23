"""Command line entry point: play a battle against a bot from the terminal.

trainerlab-play --opponent survival --seed 42
trainerlab-play --team Dragapult,Snorlax,Toxapex,Excadrill,Blissey,Corviknight --no-hints
"""

import argparse
import random
import shutil
import sys
from contextlib import ExitStack

from trainerlab import _engine
from trainerlab.battle import play_battle
from trainerlab.environment import BattleEnvironment, random_team_pair
from trainerlab.human import Ask, HumanPlayer
from trainerlab.render import Style, describe_board, narrate
from trainerlab.screen import SPEEDS, Screen, Write
from trainerlab.stats import BattleStats
from trainerlab.tournament import BOTS

ME = 0
RESULTS = {ME: "You won!", 1 - ME: "You lost.", None: "It's a draw."}


def _stdout(text: str) -> None:
    sys.stdout.write(text)
    sys.stdout.flush()


def main(argv: list[str] | None = None, ask: Ask = input, write: Write = _stdout) -> int:
    parser = argparse.ArgumentParser(description="Play a battle against a bot.")
    parser.add_argument("--opponent", choices=sorted(BOTS), default="survival")
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
        return _play(env, args.opponent, seed, team, screen, style, ask, not args.no_hints)


def title_for(before: _engine.BattleState, after: _engine.BattleState) -> str:
    if after.turn != before.turn:
        return f"Turn {after.turn}"
    return "Battle start" if after.turn == 0 else "Switch-in"


def _play(
    env: BattleEnvironment,
    opponent: str,
    seed: int,
    team: list[str],
    screen: Screen,
    style: Style,
    ask: Ask,
    hints: bool,
) -> int:
    screen.clear()
    screen.message(f"Seed {seed} (replay this battle with --seed {seed}). Opponent: {opponent}.")
    screen.message("Your team: " + ", ".join(team))
    stats = BattleStats()
    before = env.observe(ME)

    def tell(events: list[_engine.Event]) -> None:
        nonlocal before
        after = env.observe(ME)
        lines = narrate(list(events), before, after, ME, style)
        if lines:
            screen.narrate(title_for(before, after), lines)
        stats.record(lines)
        before = after

    tell(env.opening_events)
    player = HumanPlayer(ask, screen, style, hints=hints)
    try:
        winner = play_battle(env, player, BOTS[opponent](seed), tell)
    except (EOFError, KeyboardInterrupt):
        screen.message("Battle abandoned.")
        return 130

    final = env.observe(ME)
    screen.clear()
    screen.panel("Final board", describe_board(final, ME, style))
    screen.panel("Last turn", screen.take_history())
    screen.panel(RESULTS[winner], stats.summary(final, ME, env.turn))
    screen.message(RESULTS[winner])
    return 0
