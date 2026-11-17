"""`trainerlab duel`: plays paired battles between two bots and prints bot A's score.

trainerlab duel calculator survivor --pairs 5000 --from 30000
"""

import argparse

from trainerlab.commands.console import stdout
from trainerlab.commands.options import add_bots, add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.experiments.tournament import duel
from trainerlab.game.pairs import span
from trainerlab.terminal.screen import Write


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab duel", description="Score bot A against bot B over paired battles."
    )
    add_bots(parser)
    add_pairs(parser)
    add_jobs(parser)
    args = parser.parse_args(argv)

    seeds = seeds_of(args)
    progress = parallel.progress_on_terminal(len(seeds))
    result = duel(args.bot_a, args.bot_b, seeds, args.jobs, progress)
    low, high = result.interval
    write(
        f"{args.bot_a} vs {args.bot_b}: {result.score:.3f} [{low:.3f}, {high:.3f}] "
        f"({result.wins} wins, {result.draws} draws, {result.losses} losses, "
        f"seeds {span(seeds)})\n"
    )
    return 0
