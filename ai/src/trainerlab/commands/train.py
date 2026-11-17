"""`trainerlab train`: fits a win probability model on a dataset, and measures it on unseen battles.

trainerlab train calculator-vs-survivor-40000-41999.npz --model logistic
trainerlab train a.npz b.npz c.npz --out gen2.npz        # several datasets, put together
"""

import argparse
import sys
from collections.abc import Callable
from pathlib import Path

from trainerlab.learning.fitting import MODELS, DatasetError, default_name, fit, load

Write = Callable[[str], None]


def _stdout(text: str) -> None:
    sys.stdout.write(text)


def main(argv: list[str] | None = None, write: Write = _stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab train", description="Fit and measure a win probability model."
    )
    parser.add_argument(
        "datasets", type=Path, nargs="+", help="one or more .npz files made by trainerlab dataset"
    )
    parser.add_argument("--model", choices=list(MODELS), default="logistic")
    parser.add_argument("--out", type=Path, help="the model file (default: DATASET-MODEL.npz)")
    args = parser.parse_args(argv)
    try:
        rows = load(args.datasets)
    except DatasetError as error:
        write(f"{error}\n")
        return 2

    result = fit(rows, args.model)
    write(
        f"{', '.join(path.name for path in args.datasets)}: {result.train_rows} rows to train "
        f"({result.train_seeds} seeds), {result.test_rows} to test "
        f"({result.test_seeds} seeds, the multiples of 5)\n\n"
    )
    if result.held_out is not None:
        passes = result.model.epochs
        write(
            f"{result.held_out} of the training rows held out to stop early: "
            f"best after {passes} pass{'es' if passes > 1 else ''}\n\n"
        )
    write(f"{'':<18}{'log loss':>10}{'Brier':>8}{'accuracy':>10}\n")
    for score in result.scores:
        write(
            f"{score.label:<18}{score.log_loss:>10.3f}{score.brier:>8.3f}{score.accuracy:>10.3f}\n"
        )

    out = args.out or default_name(args.datasets, args.model)
    result.model.save(out)
    write(f"\n-> {out}\n")
    return 0
