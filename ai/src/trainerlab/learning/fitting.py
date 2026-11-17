"""Fitting a win probability model on datasets, and scoring it on battles it has never seen.

The battles whose seed is a multiple of 5 are held out for the test: 20 % of them, never seen in
training. The model is compared there with a constant, the share of wins in training. A model
that stops early also holds out, from its training battles, those whose seed ends in 1 or 6: it
is scored on them after each pass over the data, and the best pass is kept.
"""

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt

from trainerlab.learning.model import (
    TEST_EVERY,
    WinModel,
    accuracy,
    brier,
    is_test,
    log_loss,
    train,
)

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


class DatasetError(ValueError):
    """Datasets that cannot be trained on, with a sentence that says why."""


@dataclass(frozen=True)
class Rows:
    """The rows of one or more datasets, put together."""

    x: npt.NDArray[np.floating]
    y: npt.NDArray[np.floating]
    seeds: npt.NDArray[np.integer]
    feature_names: tuple[str, ...]


@dataclass(frozen=True)
class Score:
    label: str
    log_loss: float
    brier: float
    accuracy: float


@dataclass(frozen=True)
class Fit:
    """A trained model, how the rows were split, and its scores."""

    model: WinModel
    train_rows: int
    train_seeds: int
    test_rows: int
    test_seeds: int
    held_out: int | None  # rows held out to stop early, if the model did
    scores: list[Score]


def load(paths: list[Path]) -> Rows:
    missing = [path for path in paths if not path.is_file()]
    if missing:
        raise DatasetError(f"No such dataset: {missing[0]}")
    files = [np.load(path) for path in paths]
    names = tuple(str(name) for name in files[0]["feature_names"])
    if any(tuple(str(n) for n in f["feature_names"]) != names for f in files[1:]):
        raise DatasetError("These datasets do not have the same features: build them again")
    x, y, seeds = (np.concatenate([f[key] for f in files]) for key in ("x", "y", "seed"))
    return Rows(x, y, seeds, names)


def fit(rows: Rows, model: str) -> Fit:
    """Trains the model named, on every battle but the test ones, and scores it."""
    x, y, seeds = rows.x, rows.y, rows.seeds
    test = is_test(seeds)
    fit_rows, validation = ~test, None
    hidden = MODELS[model]
    if model in EARLY_STOP:
        held = fit_rows & (seeds % TEST_EVERY == 1)
        fit_rows, validation = fit_rows & ~held, (x[held], y[held])
        trained = train(
            x[fit_rows],
            y[fit_rows],
            rows.feature_names,
            hidden=hidden,
            epochs=MOST_PASSES,
            patience=PATIENCE,
            validation=validation,
        )
    else:
        trained = train(x[fit_rows], y[fit_rows], rows.feature_names, hidden=hidden)
    constant = np.full(test.sum(), y[~test].mean())
    scores = [
        _score(label, p, labels)
        for label, p, labels in (
            ("constant, test", constant, y[test]),
            (f"{trained.kind}, train", trained.predict(x[fit_rows]), y[fit_rows]),
            (f"{trained.kind}, test", trained.predict(x[test]), y[test]),
        )
    ]
    return Fit(
        model=trained,
        train_rows=int((~test).sum()),
        train_seeds=len(np.unique(seeds[~test])),
        test_rows=int(test.sum()),
        test_seeds=len(np.unique(seeds[test])),
        held_out=None if validation is None else len(validation[1]),
        scores=scores,
    )


def _score(label: str, p: npt.NDArray[np.float64], labels: npt.NDArray[np.floating]) -> Score:
    return Score(label, log_loss(p, labels), brier(p, labels), accuracy(p, labels))


def default_name(datasets: list[Path], model: str) -> Path:
    first, more = datasets[0], len(datasets) - 1
    return first.with_name(f"{first.stem}{f'-plus-{more}' if more else ''}-{model}.npz")
