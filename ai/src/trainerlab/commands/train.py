"""`trainerlab train`: fits a win probability model on a dataset, and measures it on unseen battles.

    trainerlab train calculator-vs-survivor-40000-41999.npz --model logistic
    trainerlab train a.npz b.npz c.npz --out gen2.npz        # several datasets, put together

The battles whose seed is a multiple of 5 are held out for the test: 20 % of them, never seen in
training. The model is compared there with a constant, the share of wins in training. A model
that stops early also holds out, from its training battles, those whose seed ends in 1 or 6: it
is scored on them after each pass over the data, and the best pass is kept.
"""

import argparse
import sys
from collections.abc import Callable
from pathlib import Path

import numpy as np

from trainerlab.learning.model import TEST_EVERY, accuracy, brier, is_test, log_loss, train

Write = Callable[[str], None]

# Fixed before any measurement: a logistic regression, and one hidden layer of 64 ReLU units,
# trained for 10 passes; then the same network stopped early (at most 50 passes, patience 3).
MODELS: dict[str, tuple[int, ...]] = {"logistic": (), "network": (64,), "network-stop": (64,)}
DESCRIPTIONS = {
    "logistic": "logistic regression",
    "network": "one hidden layer of 64 units, 10 passes",
    "network-stop": "one hidden layer of 64 units, stopped early on validation battles",
}
EARLY_STOP = {"network-stop"}
MOST_PASSES, PATIENCE = 50, 3


def default_name(datasets: list[Path], model: str) -> Path:
    first, more = datasets[0], len(datasets) - 1
    return first.with_name(f"{first.stem}{f'-plus-{more}' if more else ''}-{model}.npz")


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
    missing = [path for path in args.datasets if not path.is_file()]
    if missing:
        write(f"No such dataset: {missing[0]}\n")
        return 2

    files = [np.load(path) for path in args.datasets]
    names = tuple(str(name) for name in files[0]["feature_names"])
    if any(tuple(str(n) for n in f["feature_names"]) != names for f in files[1:]):
        write("These datasets do not have the same features: build them again\n")
        return 2
    x, y, seeds = (np.concatenate([f[key] for f in files]) for key in ("x", "y", "seed"))
    test = is_test(seeds)
    write(
        f"{', '.join(path.name for path in args.datasets)}: {(~test).sum()} rows to train "
        f"({len(np.unique(seeds[~test]))} seeds), {test.sum()} to test "
        f"({len(np.unique(seeds[test]))} seeds, the multiples of 5)\n\n"
    )

    fit, validation = ~test, None
    hidden = MODELS[args.model]
    if args.model in EARLY_STOP:
        held = fit & (seeds % TEST_EVERY == 1)
        fit, validation = fit & ~held, (x[held], y[held])
        model = train(
            x[fit],
            y[fit],
            names,
            hidden=hidden,
            epochs=MOST_PASSES,
            patience=PATIENCE,
            validation=validation,
        )
    else:
        model = train(x[fit], y[fit], names, hidden=hidden)
    if validation is not None:
        write(
            f"{len(validation[1])} of the training rows held out to stop early: "
            f"best after {model.epochs} pass{'es' if model.epochs > 1 else ''}\n\n"
        )
    constant = np.full(test.sum(), y[~test].mean())
    write(f"{'':<18}{'log loss':>10}{'Brier':>8}{'accuracy':>10}\n")
    for label, p, labels in (
        ("constant, test", constant, y[test]),
        (f"{model.kind}, train", model.predict(x[fit]), y[fit]),
        (f"{model.kind}, test", model.predict(x[test]), y[test]),
    ):
        write(
            f"{label:<18}{log_loss(p, labels):>10.3f}{brier(p, labels):>8.3f}"
            f"{accuracy(p, labels):>10.3f}\n"
        )

    out = args.out or default_name(args.datasets, args.model)
    model.save(out)
    write(f"\n-> {out}\n")
    return 0
