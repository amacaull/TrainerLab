"""`trainerlab verify`: plays the judge's worst blunders out to the end, both options (needs [rl]).

trainerlab verify alphazero planner --judged runs/judge-7.5-rule12.npz --worst 20 --games 200

experiments/rollouts.py says how.
"""

import argparse
from pathlib import Path

from trainerlab.commands.console import stdout
from trainerlab.commands.options import add_bots, add_jobs, count
from trainerlab.experiments import parallel, rollouts
from trainerlab.terminal.screen import Write


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab verify",
        description="Play the judge's worst blunders out to the end, with both options.",
    )
    add_bots(parser)
    parser.add_argument(
        "--judged", type=Path, required=True, help="a file of trainerlab judge, for these bots"
    )
    parser.add_argument("--worst", type=count, default=20, help="blunders checked (default: 20)")
    parser.add_argument("--games", type=count, default=200, help="games per option (200)")
    add_jobs(parser)
    args = parser.parse_args(argv)

    blunders = rollouts.worst_of(args.judged, args.worst)
    progress = parallel.progress_on_terminal(len(blunders), unit="blunders")
    checked = rollouts.verify_all(args.bot_a, args.bot_b, blunders, args.games, args.jobs, progress)
    write(
        f"the {len(checked)} worst blunders of {args.bot_a} against {args.bot_b}, "
        f"{args.games} games per option, the same dice for both\n\n"
    )
    for v in checked:
        write(
            f"  turn {v.turn:>3}: the judge said -{100 * v.loss:.1f}, the games say "
            f"{100 * v.gap:+.1f} [{100 * v.low:+.1f}, {100 * v.high:+.1f}]  {v.verdict}\n"
            f"trainerlab watch {args.bot_a} {args.bot_b} --seed {v.seed} --side {v.a_side}\n"
        )
    s = rollouts.summarize(checked)
    write(
        f"\nconfirmed {100 * s.confirmed:.0f} %, refuted {100 * s.refuted:.0f} %, the rest "
        f"unclear\nthe judge's losses, on average {100 * s.mean_loss:.1f} points; "
        f"the games' gaps {100 * s.mean_gap:.1f}\n"
    )
    return 0
