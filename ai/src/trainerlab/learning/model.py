"""The probability of winning from a position: a logistic regression, or a small neural network.

Both are the same model, written with numpy alone: layers of ReLU units, then a sigmoid. With no
hidden layer, it is exactly a logistic regression. Training minimises the cross-entropy against
the label (1 win, 0.5 draw, 0 loss) by minibatch gradient descent (Adam), from a fixed seed: the
same data gives the same model.
"""

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt

Array = npt.NDArray[np.float64]

SHIPPED = Path(__file__).parent / "models" / "win-logistic.npz"  # the model rules 6 and 7 play with


@dataclass(frozen=True)
class WinModel:
    """Standardises the features, then layers: ReLU after each but the last, then a sigmoid."""

    weights: tuple[Array, ...]
    biases: tuple[Array, ...]
    mean: Array
    std: Array
    feature_names: tuple[str, ...]
    epochs: int = 0  # passes over the data it was trained with (not saved)

    @property
    def kind(self) -> str:
        hidden = [w.shape[1] for w in self.weights[:-1]]
        return "network " + "-".join(map(str, hidden)) if hidden else "logistic"

    def predict(self, x: npt.NDArray[np.floating]) -> Array:
        """The probability of winning of each row of features."""
        z = (np.asarray(x, dtype=np.float64) - self.mean) / self.std
        for w, b in zip(self.weights[:-1], self.biases[:-1], strict=True):
            z = np.maximum(z @ w + b, 0.0)
        return _sigmoid(z @ self.weights[-1] + self.biases[-1]).ravel()

    def save(self, path: Path) -> None:
        """Writes the model as .npz: its layers, the standardisation and the feature names."""
        arrays: dict[str, Any] = {f"w{i}": w for i, w in enumerate(self.weights)}
        arrays |= {f"b{i}": b for i, b in enumerate(self.biases)}
        arrays |= {
            "mean": self.mean,
            "std": self.std,
            "feature_names": np.array(self.feature_names),
        }
        np.savez(path, **arrays)  # Any: numpy's stub also matches allow_pickle

    @classmethod
    def load(cls, path: Path) -> "WinModel":
        data = np.load(path)
        count = sum(1 for key in data.files if key.startswith("w"))
        return cls(
            weights=tuple(data[f"w{i}"] for i in range(count)),
            biases=tuple(data[f"b{i}"] for i in range(count)),
            mean=data["mean"],
            std=data["std"],
            feature_names=tuple(str(name) for name in data["feature_names"]),
        )


def train(
    x: npt.NDArray[np.floating],
    y: npt.NDArray[np.floating],
    feature_names: tuple[str, ...],
    *,
    hidden: tuple[int, ...] = (),
    epochs: int = 10,
    batch: int = 1024,
    rate: float = 1e-3,
    decay: float = 1e-4,
    seed: int = 0,
    validation: tuple[npt.NDArray[np.floating], npt.NDArray[np.floating]] | None = None,
    patience: int = 3,
) -> WinModel:
    """Fits the model to rows `x` and labels `y`. `hidden=()` is a logistic regression.

    With `validation` rows and labels, training stops early: after each pass, the model is
    scored on them, and the best one is kept once `patience` passes in a row did no better.
    `epochs` is then the most passes allowed.
    """
    rng = np.random.default_rng(seed)
    rows: Array = np.asarray(x, dtype=np.float64)
    labels: Array = np.asarray(y, dtype=np.float64).reshape(-1, 1)
    mean: Array = rows.mean(axis=0)
    std: Array = rows.std(axis=0)
    std[std == 0] = 1.0  # a constant feature: left as it is
    z = (rows - mean) / std

    sizes = [z.shape[1], *hidden, 1]
    weights = [
        _initial(rng, n_in, n_out, last=i == len(hidden))
        for i, (n_in, n_out) in enumerate(zip(sizes, sizes[1:], strict=False))
    ]
    biases = [np.zeros(n_out) for n_out in sizes[1:]]
    params = weights + biases
    first = [np.zeros_like(p) for p in params]  # Adam's running mean of the gradients
    second = [np.zeros_like(p) for p in params]  # and of their squares
    step = 0
    best: WinModel | None = None
    best_loss, waited = np.inf, 0
    for epoch in range(1, epochs + 1):
        order = rng.permutation(len(z))
        for start in range(0, len(z), batch):
            picked = order[start : start + batch]
            grads = _gradients(weights, biases, z[picked], labels[picked], decay)
            step += 1
            for p, g, m, v in zip(params, grads, first, second, strict=True):
                # Adam's update, with its usual constants: beta1 0.9, beta2 0.999, epsilon 1e-8.
                m[...] = 0.9 * m + 0.1 * g
                v[...] = 0.999 * v + 0.001 * g * g
                p -= rate * (m / (1 - 0.9**step)) / (np.sqrt(v / (1 - 0.999**step)) + 1e-8)
        if validation is None:
            continue
        now = WinModel(
            tuple(w.copy() for w in weights),
            tuple(b.copy() for b in biases),
            mean,
            std,
            feature_names,
            epoch,
        )
        loss = log_loss(now.predict(validation[0]), validation[1])
        if loss < best_loss:
            best, best_loss, waited = now, loss, 0
        else:
            waited += 1
            if waited == patience:
                break
    if best is not None:
        return best
    return WinModel(tuple(weights), tuple(biases), mean, std, feature_names, epochs)


def _initial(rng: np.random.Generator, n_in: int, n_out: int, *, last: bool) -> Array:
    if last:
        return np.zeros((n_in, n_out))  # the output layer starts at a probability of 0.5
    return rng.normal(0.0, np.sqrt(2.0 / n_in), (n_in, n_out))  # He, for ReLU


def _gradients(
    weights: list[Array], biases: list[Array], z: Array, y: Array, decay: float
) -> list[Array]:
    """Backpropagation of the mean cross-entropy, with L2 decay on the weights."""
    activations = [z]
    for w, b in zip(weights[:-1], biases[:-1], strict=True):
        activations.append(np.maximum(activations[-1] @ w + b, 0.0))
    p = _sigmoid(activations[-1] @ weights[-1] + biases[-1])
    delta = (p - y) / len(z)  # d(cross-entropy)/d(logit) of a sigmoid
    dw: list[Array] = []
    db: list[Array] = []
    for layer in reversed(range(len(weights))):
        dw.insert(0, activations[layer].T @ delta + decay * weights[layer])
        db.insert(0, delta.sum(axis=0))
        if layer:
            delta = (delta @ weights[layer].T) * (activations[layer] > 0)
    return dw + db


def _sigmoid(t: Array) -> Array:
    return 1.0 / (1.0 + np.exp(-np.clip(t, -30, 30)))


# ---------------------------------------------------------------------------------------------
# Measuring a model


def log_loss(p: Array, y: npt.NDArray[np.floating]) -> float:
    """Mean cross-entropy: 0.693 for a coin toss, lower is better."""
    p = np.clip(p, 1e-12, 1 - 1e-12)
    return float(-np.mean(y * np.log(p) + (1 - y) * np.log(1 - p)))


def brier(p: Array, y: npt.NDArray[np.floating]) -> float:
    """Mean squared error of the probability: 0.25 for a coin toss, lower is better."""
    return float(np.mean((p - y) ** 2))


def accuracy(p: Array, y: npt.NDArray[np.floating]) -> float:
    """Share of won or lost rows on the right side of 0.5 (draws left out)."""
    decided = y != 0.5
    return float(np.mean((p[decided] > 0.5) == (y[decided] == 1)))
