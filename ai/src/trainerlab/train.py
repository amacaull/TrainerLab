"""`trainerlab train`: fits a win probability model on a dataset, and measures it on unseen battles.

    trainerlab train calculator-vs-survivor-40000-41999.npz --model logistic

The battles whose seed is a multiple of 5 are held out for the test: 20 % of them, never seen in
training. The model is compared there with a constant, the share of wins in training.
"""

import argparse
import sys
from collections.abc import Callable
from pathlib import Path

import numpy as np

from trainerlab.model import accuracy, brier, is_test, log_loss, train

Write = Callable[[str], None]

# Fixed before any measurement: a logistic regression, and one hidden layer of 64 ReLU units.
MODELS: dict[str, tuple[int, ...]] = {"logistic": (), "network": (64,)}


def default_name(dataset: Path, model: str) -> Path:
    return dataset.with_name(f"{dataset.stem}-{model}.npz")


def _stdout(text: str) -> None:
    sys.stdout.write(text)


def main(argv: list[str] | None = None, write: Write = _stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab train", description="Fit and measure a win probability model."
    )
    parser.add_argument("dataset", type=Path, help="a .npz file made by trainerlab dataset")
    parser.add_argument("--model", choices=list(MODELS), default="logistic")
    parser.add_argument("--out", type=Path, help="the model file (default: DATASET-MODEL.npz)")
    args = parser.parse_args(argv)
    if not args.dataset.is_file():
        write(f"No such dataset: {args.dataset}\n")
        return 2

    data = np.load(args.dataset)
    x, y, seeds = data["x"], data["y"], data["seed"]
    names = tuple(str(name) for name in data["feature_names"])
    test = is_test(seeds)
    write(
        f"{args.dataset.name}: {(~test).sum()} rows to train ({len(np.unique(seeds[~test]))} "
        f"battles' seeds), {test.sum()} to test ({len(np.unique(seeds[test]))} seeds, "
        "the multiples of 5)\n\n"
    )

    model = train(x[~test], y[~test], names, hidden=MODELS[args.model])
    constant = np.full(test.sum(), y[~test].mean())
    write(f"{'':<18}{'log loss':>10}{'Brier':>8}{'accuracy':>10}\n")
    for label, p, labels in (
        ("constant, test", constant, y[test]),
        (f"{model.kind}, train", model.predict(x[~test]), y[~test]),
        (f"{model.kind}, test", model.predict(x[test]), y[test]),
    ):
        write(
            f"{label:<18}{log_loss(p, labels):>10.3f}{brier(p, labels):>8.3f}"
            f"{accuracy(p, labels):>10.3f}\n"
        )

    out = args.out or default_name(args.dataset, args.model)
    model.save(out)
    write(f"\n-> {out}\n")
    return 0
