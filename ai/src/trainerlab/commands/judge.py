"""`trainerlab judge`: a far deeper search rates each decision of bot A (needs [rl]).

trainerlab judge alphazero planner --pairs 200 --from 220000 --out runs/judge.npz

Prints how often bot A plays what the judge would, what its decisions lose on average, its
inaccuracies and blunders, and its worst decisions, each with the command to watch its battle
(experiments/judge.py says how the judge works).
"""

import argparse
from pathlib import Path
from typing import Any

import numpy as np

from trainerlab.commands.console import Write, quiet_on_closed_pipe, stdout
from trainerlab.commands.options import add_bots, add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.game.pairs import span


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab judge", description="Rate each decision of bot A with a deeper search."
    )
    add_bots(parser)
    add_pairs(parser, default=200)
    add_jobs(parser)
    parser.add_argument("--worst", type=int, default=20, help="decisions listed (default: 20)")
    parser.add_argument(
        "--out", type=Path, default=Path("judge.npz"), help="where to keep every judged decision"
    )
    args = parser.parse_args(argv)
    try:
        from trainerlab.experiments import judge
    except ImportError:
        write('trainerlab judge needs PyTorch: pip install -e "ai/.[rl]"\n')
        return 2
    if write is stdout:
        quiet_on_closed_pipe()

    seeds = seeds_of(args)
    progress = parallel.progress_on_terminal(len(seeds))
    judged = judge.judge_on_cores(args.bot_a, args.bot_b, seeds, args.jobs, progress)
    columns: dict[str, Any] = {
        field: np.array([getattr(j, field) for j in judged])
        for field in judge.Judged.__dataclass_fields__
        if field != "situations"
    }
    columns["situations"] = np.array([",".join(j.situations) for j in judged])
    np.savez(args.out, **columns)
    s = judge.summarize(judged)
    write(
        f"{args.bot_a} against {args.bot_b}, seeds {span(seeds)}: {s.decisions} decisions judged "
        f"({judge.SIMULATIONS} simulations, {judge.WORLDS} worlds, every option tried "
        f"{judge.MIN_VISITS} times or more)\n"
    )
    write(f"agrees with the judge on {100 * s.agreement:.1f} % of them\n")
    write(
        f"mean loss {100 * s.mean_loss:.2f} points of the chance to win "
        f"({100 * s.loss_in_won:.2f} in battles won, {100 * s.loss_in_lost:.2f} in lost)\n"
    )
    write(
        f"inaccuracies (5 points or more) {100 * s.inaccuracies:.1f} %, "
        f"blunders (10 points or more) {100 * s.blunders:.1f} %\n"
    )
    write(
        f"\nsituations, as the judge rates them (a mistake if half its cases lose 5 points or "
        f"more, style under a fifth, no verdict under {judge.FEWEST} cases):\n"
    )
    write(f"  {'situation':<22}{'cases':>7}{'lose 5+':>9}{'mean loss':>11}  verdict\n")
    for c in judge.calibrate(judged):
        share = f"{100 * c.confirmed:.0f} %" if c.cases else "-"
        mean = f"{100 * c.mean_loss:.2f}" if c.cases else "-"
        write(f"  {c.name:<22}{c.cases:>7}{share:>9}{mean:>11}  {c.verdict}\n")
    write(f"\nthe {args.worst} worst decisions:\n")
    for j in sorted(judged, key=lambda j: -j.loss)[: args.worst]:
        write(
            f"  -{100 * j.loss:4.1f}  turn {j.turn:>2}, {j.active} against {j.foe}: "
            f"{j.chosen_label}, the judge: {j.best_label}\n"
            f"trainerlab watch {args.bot_a} {args.bot_b} --seed {j.seed} --side {j.a_side}\n"
        )
    write(f"-> {args.out}\n")
    return 0
