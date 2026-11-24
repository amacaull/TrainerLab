"""`trainerlab truth`: the true score of teams (palier 12.2, 12.4).

trainerlab truth alphazero --pairs 300 --from 1000000 --out runs/truth-2.npz
trainerlab truth alphazero --size 6 --teams 150 --pairs 200 --from 3000000 --out runs/truth-6.npz
trainerlab truth --load runs/truth-2.npz

Each of the 1093 legal teams of two, or a sample of --teams larger teams, plays --pairs pairs
against random teams of its size (experiments/truth.py); of two, --teams N plays the first N
alone, to time a run.
"""

import argparse
from pathlib import Path

import numpy as np

from trainerlab.bots.roster import bot_spec
from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import add_jobs, count, seed
from trainerlab.experiments import parallel
from trainerlab.experiments.truth import SIZE, Team, analyze, record, teams_of

SHOWN = 10  # the best and the worst teams written out


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab truth", description="Measure every team of two against random teams."
    )
    parser.add_argument("bot", type=bot_spec, nargs="?", help="the bot, playing both sides")
    parser.add_argument("--load", type=Path, help="analyse the battles kept in this file")
    parser.add_argument("--out", type=Path, default=Path("truth.npz"), help="where to keep them")
    parser.add_argument("--pairs", type=count, default=100, help="pairs per team (100)")
    parser.add_argument("--size", type=int, choices=range(2, 7), default=SIZE, help="(2)")
    parser.add_argument(
        "--teams", type=count, help="of two, the first N alone (all: 1093); larger, how many"
    )
    parser.add_argument("--from", dest="first_seed", type=seed, default=0, metavar="SEED")
    add_jobs(parser)
    args = parser.parse_args(argv)
    if (args.bot is None) == (args.load is None):
        parser.error("give a bot to play, or --load FILE to read")
    if args.load is None and args.size != SIZE and args.teams is None:
        parser.error(f"teams of {args.size} are sampled: give --teams")

    if args.load is not None:
        saved = np.load(args.load)
        index, points = saved["team"], saved["points"]
        first, pairs = int(saved["first"]), int(saved["pairs"])
        # Files of palier 12.2 keep no names: every team of two, in order.
        listed = [list(t) for t in saved["names"]] if "names" in saved else teams_of(SIZE, None)
    else:
        first, pairs = args.first_seed, args.pairs
        listed = teams_of(args.size, args.teams)
        progress = parallel.progress_on_terminal(len(listed) * pairs)
        index, points = record(
            args.bot,
            first=first,
            pairs=pairs,
            jobs=args.jobs,
            count=args.teams,
            size=args.size,
            progress=progress,
        )
        names = np.array(listed)
        np.savez(args.out, team=index, points=points, first=first, pairs=pairs, names=names)

    result = analyze(index, points, listed)
    teams = len(result.teams)
    write(
        f"{len(points)} battles: {teams} teams of {len(listed[0])}, {pairs} pairs each, "
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
