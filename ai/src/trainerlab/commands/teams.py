"""`trainerlab teams`: does the composition of a team decide battles? One bot against itself.

trainerlab teams alphazero --pairs 3000 --from 190000 --out runs/teams.npz
trainerlab teams --load runs/teams.npz

The first form plays the pairs and keeps every battle's result in --out; the second reads such a
file again, without playing (experiments/teams.py says what is measured).
"""

import argparse
from pathlib import Path

import numpy as np

from trainerlab.bots.roster import bot_spec
from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.experiments.teams import analyze, record


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab teams",
        description="Measure how much the composition of a team decides battles.",
    )
    parser.add_argument("bot", type=bot_spec, nargs="?", help="the bot, playing both sides")
    parser.add_argument("--load", type=Path, help="analyse the battles kept in this file")
    parser.add_argument(
        "--out", type=Path, default=Path("teams.npz"), help="where to keep the battles"
    )
    add_pairs(parser)
    add_jobs(parser)
    args = parser.parse_args(argv)
    if (args.bot is None) == (args.load is None):
        parser.error("give a bot to play, or --load FILE to read")

    if args.load is not None:
        saved = np.load(args.load)
        seeds, points = saved["seed"], saved["points"]
    else:
        pairs = seeds_of(args)
        seeds, points = record(
            args.bot, pairs, args.jobs, parallel.progress_on_terminal(len(pairs))
        )
        np.savez(args.out, seed=seeds, points=points)

    result = analyze(seeds, points)
    write(f"{result.battles} battles, seeds {seeds[0]}-{seeds[-1]}\n")
    write(
        f"the same team won both battles of a pair: {result.agreement:.3f} "
        f"[{result.agreement_low:.3f}, {result.agreement_high:.3f}] of {result.decided_pairs} "
        f"pairs without a draw (0.5 if the teams did not matter)\n"
    )
    write(
        f"the composition alone calls the winner of {result.accuracy:.3f} of the held-out "
        f"battles (log loss {result.log_loss:.3f}, 0.693 for a coin)\n"
    )
    outside = sum(1 for s in result.species if s.low > 0.5 or s.high < 0.5)
    write(
        f"species by the share of points of the teams that hold them ({outside} of "
        f"{len(result.species)} outside 0.5 at 95 %, about {len(result.species) / 20:.0f} by "
        f"chance alone):\n"
    )
    for s in result.species:
        write(f"  {s.name:<16} {s.score:.3f} [{s.low:.3f}, {s.high:.3f}]  {s.battles} battles\n")
    if args.load is None:
        write(f"-> {args.out}\n")
    return 0
