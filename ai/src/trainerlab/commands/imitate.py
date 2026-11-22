"""`trainerlab imitate`: bot A's decisions, encoded for the network to learn from.

trainerlab imitate forecaster calculator --pairs 2000 --from 110000
"""

import argparse
from pathlib import Path

from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import add_bots, add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.experiments.imitation import default_name, generate, save
from trainerlab.game.pairs import span


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab imitate", description="Encode bot A's decisions for the network."
    )
    add_bots(parser)
    add_pairs(parser)
    add_jobs(parser)
    parser.add_argument(
        "--out", type=Path, help="the .npz file (default: imitate-A-vs-B-SEEDS.npz)"
    )
    args = parser.parse_args(argv)

    seeds = seeds_of(args)
    out = args.out or Path(default_name(args.bot_a, args.bot_b, seeds))
    rows = generate(
        args.bot_a, args.bot_b, seeds, args.jobs, parallel.progress_on_terminal(len(seeds))
    )
    save(out, rows)
    write(
        f"{args.bot_a} against {args.bot_b}, seeds {span(seeds)}: "
        f"{len(rows['action'])} decisions by {args.bot_a} -> {out}\n"
    )
    return 0
