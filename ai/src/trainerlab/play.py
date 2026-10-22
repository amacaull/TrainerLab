"""Command line entry point: play a battle against a bot from the terminal.

trainerlab-play --opponent survival --seed 42
trainerlab-play --team Dragapult,Snorlax,Toxapex,Excadrill,Blissey,Corviknight
"""

import argparse
import random
import sys

from trainerlab import _engine
from trainerlab.battle import play_battle
from trainerlab.environment import BattleEnvironment, random_team_pair
from trainerlab.human import Ask, HumanPlayer, Say
from trainerlab.render import Style, describe_events
from trainerlab.tournament import BOTS

ME = 0


def main(argv: list[str] | None = None, ask: Ask = input, say: Say = print) -> int:
    parser = argparse.ArgumentParser(description="Play a battle against a bot.")
    parser.add_argument("--opponent", choices=sorted(BOTS), default="survival")
    parser.add_argument("--seed", type=int, help="replays the same battle (default: random)")
    parser.add_argument("--team", help="your Pokemon, comma-separated (default: random)")
    parser.add_argument("--no-color", action="store_true", help="plain text output")
    args = parser.parse_args(argv)

    seed = args.seed if args.seed is not None else random.SystemRandom().randrange(1_000_000)
    style = Style(enabled=not args.no_color and sys.stdout.isatty())
    random_team, foe_team = random_team_pair(seed)
    team = args.team.split(",") if args.team else random_team
    try:
        env = BattleEnvironment(team, foe_team, seed=seed)
    except (ValueError, _engine.TeamError) as error:
        say(f"Invalid team: {error}")
        return 2

    say(f"Seed {seed} (replay this battle with --seed {seed}). Opponent: {args.opponent}.")
    say("Your team: " + ", ".join(team))

    before = env.observe(ME)

    def tell(events: list[_engine.Event]) -> None:
        nonlocal before
        after = env.observe(ME)
        for line in describe_events(list(events), before, after, ME, style):
            say(line)
        before = after

    tell(env.opening_events)
    try:
        winner = play_battle(env, HumanPlayer(ask, say, style), BOTS[args.opponent](seed), tell)
    except (EOFError, KeyboardInterrupt):
        say("Battle abandoned.")
        return 130
    say({ME: "You won!", 1 - ME: "You lost.", None: "It's a draw."}[winner])
    return 0
