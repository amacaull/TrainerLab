"""`trainerlab duel`: plays paired battles between two bots and prints bot A's score.

trainerlab duel calculator survivor --pairs 5000 --from 30000
"""

import argparse

from trainerlab.harness import run_tournament
from trainerlab.options import add_bots, add_pairs, seeds_of, show
from trainerlab.roster import BOTS


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab duel", description="Score bot A against bot B over paired battles."
    )
    add_bots(parser)
    add_pairs(parser, 500)
    args = parser.parse_args(argv)

    seeds = seeds_of(args)
    result = run_tournament(
        BOTS[args.bot_a], BOTS[args.bot_b], pairs=len(seeds), first_seed=seeds[0]
    )
    low, high = result.interval
    print(
        f"{args.bot_a} vs {args.bot_b}: {result.score:.3f} [{low:.3f}, {high:.3f}] "
        f"({result.wins} wins, {result.draws} draws, {result.losses} losses, "
        f"seeds {show(seeds)})"
    )
    return 0
