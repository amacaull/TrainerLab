"""`trainerlab dataset`: turns the decisions of a series of battles into a learning dataset.

trainerlab dataset calculator survivor --pairs 2000 --from 40000
"""

import argparse
from pathlib import Path

from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import add_bots, add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.experiments.dataset import default_name, generate, save
from trainerlab.game.pairs import span


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab dataset", description="Turn the decisions of battles into a dataset."
    )
    add_bots(parser)
    add_pairs(parser)
    add_jobs(parser)
    parser.add_argument(
        "--out", type=Path, help="the .npz file to write (default: BOT_A-vs-BOT_B-SEEDS.npz)"
    )
    args = parser.parse_args(argv)

    seeds = seeds_of(args)
    out = args.out or Path(default_name(args.bot_a, args.bot_b, seeds))
    progress = parallel.progress_on_terminal(len(seeds))
    data = generate(args.bot_a, args.bot_b, seeds, args.jobs, progress)
    save(out, data, (args.bot_a, args.bot_b))
    rows, width = data["x"].shape
    write(
        f"{args.bot_a} against {args.bot_b}, seeds {span(seeds)}: "
        f"{rows} decisions x {width} features, mean label {data['y'].mean():.3f} -> {out}\n"
    )
    return 0
