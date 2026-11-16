"""`trainerlab duel`: plays paired battles between two bots and prints bot A's score.

trainerlab duel calculator survivor --pairs 5000 --from 30000
"""

import argparse

from trainerlab import parallel
from trainerlab.harness import TournamentResult, run_tournament
from trainerlab.options import add_bots, add_jobs, add_pairs, seeds_of, show
from trainerlab.roster import BOTS


def play(bot_a: str, bot_b: str, seeds: range) -> TournamentResult:
    """One block of pairs; a process of its own when they are spread over the cores."""
    return run_tournament(BOTS[bot_a], BOTS[bot_b], pairs=len(seeds), first_seed=seeds[0])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab duel", description="Score bot A against bot B over paired battles."
    )
    add_bots(parser)
    add_pairs(parser, 500)
    add_jobs(parser)
    args = parser.parse_args(argv)

    seeds = seeds_of(args)
    progress = parallel.progress_on_terminal(len(seeds))
    parts = parallel.run(play, args.bot_a, args.bot_b, seeds, args.jobs, progress)
    result = TournamentResult(
        wins=sum(p.wins for p in parts),
        draws=sum(p.draws for p in parts),
        losses=sum(p.losses for p in parts),
    )
    low, high = result.interval
    print(
        f"{args.bot_a} vs {args.bot_b}: {result.score:.3f} [{low:.3f}, {high:.3f}] "
        f"({result.wins} wins, {result.draws} draws, {result.losses} losses, "
        f"seeds {show(seeds)})"
    )
    return 0
