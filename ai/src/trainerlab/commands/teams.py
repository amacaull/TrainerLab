"""`trainerlab teams`: does the composition of a team decide battles? One bot against itself.

trainerlab teams alphazero --pairs 3000 --from 190000 --out runs/teams.npz
trainerlab teams alphazero --size 2 --pairs 3000 --from 340000 --out runs/teams-2.npz
trainerlab teams --load runs/teams.npz
trainerlab teams --load runs/teams-6b.npz --strengths src/trainerlab/game/species-strength.json
trainerlab teams --load runs/teams-6b.npz --leads src/trainerlab/game/lead-strength.json

The first form plays the pairs and keeps every battle's result in --out; the second reads such a
file again, without playing (experiments/teams.py says what is measured). --strengths also writes
each species' score, as the bot of `trainerlab play` builds its team with them; --leads each
lead's, as a bot that chooses its lead expects the foe's from them.
"""

import argparse
import json
from pathlib import Path

import numpy as np

from trainerlab.bots.roster import bot_spec
from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.experiments.teams import analyze, lead_scores, record


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
    parser.add_argument(
        "--size", type=int, choices=range(1, 7), default=6, help="Pokemon per team (6)"
    )
    parser.add_argument("--strengths", type=Path, help="write each species' score to this file")
    parser.add_argument("--leads", type=Path, help="write each lead's score to this file")
    add_pairs(parser)
    add_jobs(parser)
    args = parser.parse_args(argv)
    if (args.bot is None) == (args.load is None):
        parser.error("give a bot to play, or --load FILE to read")

    if args.load is not None:
        saved = np.load(args.load)
        seeds, points = saved["seed"], saved["points"]
        size = int(saved["size"]) if "size" in saved else 6  # files of palier 7: teams of 6
    else:
        pairs, size = seeds_of(args), args.size
        progress = parallel.progress_on_terminal(len(pairs))
        seeds, points = record(args.bot, pairs, args.jobs, progress, size)
        np.savez(args.out, seed=seeds, points=points, size=size)

    result = analyze(seeds, points, size)
    write(f"{result.battles} battles of teams of {size}, seeds {seeds[0]}-{seeds[-1]}\n")
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
    if args.strengths is not None:
        scores = {s.name: s.score for s in result.species}
        args.strengths.write_text(json.dumps(scores, indent=1) + "\n")
        write(f"-> {args.strengths}\n")
    if args.leads is not None:
        args.leads.write_text(json.dumps(lead_scores(seeds, points, size), indent=1) + "\n")
        write(f"-> {args.leads}\n")
    if args.load is None:
        write(f"-> {args.out}\n")
    return 0
