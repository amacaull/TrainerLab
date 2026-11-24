"""`trainerlab truth`: the true score of every team of two (palier 12.2).

trainerlab truth alphazero --pairs 100 --from 1000000 --out runs/truth-2.npz
trainerlab truth --load runs/truth-2.npz

Each of the 1093 legal teams of two plays --pairs pairs against random teams of two
(experiments/truth.py); --teams N plays the first N alone, to time a run.
"""

import argparse
from pathlib import Path

import numpy as np

from trainerlab.bots.roster import bot_spec
from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import add_jobs, count, seed
from trainerlab.experiments import parallel
from trainerlab.experiments.truth import SIZE, Team, analyze, every_team, record

SHOWN = 10  # the best and the worst teams written out


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab truth", description="Measure every team of two against random teams."
    )
    parser.add_argument("bot", type=bot_spec, nargs="?", help="the bot, playing both sides")
    parser.add_argument("--load", type=Path, help="analyse the battles kept in this file")
    parser.add_argument("--out", type=Path, default=Path("truth.npz"), help="where to keep them")
    parser.add_argument("--pairs", type=count, default=100, help="pairs per team (100)")
    parser.add_argument("--teams", type=count, help="the first N teams alone (all: 1093)")
    parser.add_argument("--from", dest="first_seed", type=seed, default=0, metavar="SEED")
    add_jobs(parser)
    args = parser.parse_args(argv)
    if (args.bot is None) == (args.load is None):
        parser.error("give a bot to play, or --load FILE to read")

    if args.load is not None:
        saved = np.load(args.load)
        index, points = saved["team"], saved["points"]
        first, pairs = int(saved["first"]), int(saved["pairs"])
    else:
        first, pairs = args.first_seed, args.pairs
        teams = args.teams or len(every_team())
        progress = parallel.progress_on_terminal(teams * pairs)
        index, points = record(
            args.bot, first=first, pairs=pairs, jobs=args.jobs, count=teams, progress=progress
        )
        np.savez(args.out, team=index, points=points, first=first, pairs=pairs)

    result = analyze(index, points)
    teams = len(result.teams)
    write(
        f"{len(points)} battles: {teams} teams of {SIZE}, {pairs} pairs each, "
        f"seeds {first}-{first + teams * pairs - 1}\n"
    )
    write(
        f"the teams' scores spread by {result.spread:.3f} (standard deviation), one score's "
        f"error is about {result.noise:.3f}: {result.explained:.0%} of it is the teams', the "
        f"rest the dice\n"
    )
    write("the best teams:\n")
    for team in result.teams[:SHOWN]:
        write(_line(team))
    write("the worst teams:\n")
    for team in result.teams[-SHOWN:]:
        write(_line(team))
    if args.load is None:
        write(f"-> {args.out}\n")
    return 0


def _line(team: Team) -> str:
    names = " + ".join(team.names)
    return f"  {names:<30} {team.score:.3f} [{team.low:.3f}, {team.high:.3f}]\n"
