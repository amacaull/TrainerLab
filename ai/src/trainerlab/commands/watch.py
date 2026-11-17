"""`trainerlab watch`: replays a battle between two bots, with what each bot saw.

    trainerlab watch survivor matcher --losses --pairs 50 --from 20000
    trainerlab watch survivor matcher --seed 20017 --side 1

The first form lists the battles of a tournament that bot A lost, as commands to watch each one.
The second replays one of them (terminal/commentary.py).
"""

import argparse

from trainerlab.bots.roster import factory
from trainerlab.commands.console import quiet_on_closed_pipe, stdout
from trainerlab.commands.options import SCANNED_PAIRS, add_bots, add_pairs, seeds_of
from trainerlab.experiments.tournament import losses
from trainerlab.game.environment import random_team_pair
from trainerlab.game.pairs import outcome_of, pair_battles
from trainerlab.terminal.commentary import replay
from trainerlab.terminal.screen import Write

RESULTS = {"win": "won", "draw": "drew", "loss": "lost"}


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab watch", description="Replay a battle between two bots."
    )
    add_bots(parser)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--seed", type=int, help="the pair of the battle to replay")
    mode.add_argument("--losses", action="store_true", help="list the battles bot A lost")
    parser.add_argument("--side", type=int, choices=(0, 1), default=0, help="bot A's side")
    add_pairs(parser, SCANNED_PAIRS, what="to scan with --losses")
    args = parser.parse_args(argv)
    if write is stdout:
        quiet_on_closed_pipe()

    if args.losses:
        _list_losses(args.bot_a, args.bot_b, seeds_of(args), write)
    else:
        _watch(args.bot_a, args.bot_b, args.seed, args.side, write)
    return 0


def _list_losses(bot_a: str, bot_b: str, seeds: range, write: Write) -> None:
    lost = losses(bot_a, bot_b, seeds)
    for seed, a_side in lost:
        write(f"trainerlab watch {bot_a} {bot_b} --seed {seed} --side {a_side}\n")
    write(f"{bot_a} lost {len(lost)} of {2 * len(seeds)} battles against {bot_b}.\n")


def _watch(bot_a: str, bot_b: str, seed: int, a_side: int, write: Write) -> None:
    team0, team1 = random_team_pair(seed)
    battles = pair_battles(factory(bot_a), factory(bot_b), seed=seed, team0=team0, team1=team1)
    _, env, bot0, bot1 = next(battle for battle in battles if battle[0] == a_side)
    names = {a_side: f"A, {bot_a}", 1 - a_side: f"B, {bot_b}"}
    teams = (team0, team1)
    write(f"Seed {seed}. Bot A = {bot_a}, side {a_side}. Bot B = {bot_b}, side {1 - a_side}.\n")
    write("A's team: " + ", ".join(teams[a_side]) + "\n")
    write("B's team: " + ", ".join(teams[1 - a_side]) + "\n")
    write("In the events, 'your' is bot A and 'the foe' is bot B.\n")
    winner = replay(env, (bot0, bot1), (names[0], names[1]), a_side, write)
    result = RESULTS[outcome_of(winner, a_side)]
    write(f"\nBot A ({bot_a}) {result} in {env.turn} turns.\n")
