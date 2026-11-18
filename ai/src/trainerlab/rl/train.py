"""Pre-training the network by imitation: play as the bot imitated, and predict who wins.

The loss adds the cross-entropy of the policy against the option the bot chose, and the squared
error of the value against the result of its battle. The battles whose seed is a multiple of 5
are held out for the test, as for the win model. Settings fixed before any measurement.

Needs PyTorch: pip install -e "ai/.[rl]".
"""

from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt
import torch
from torch.nn import functional

from trainerlab.rl.net import PolicyValueNet, Shape

TEST_EVERY = 5
EPOCHS = 8
BATCH = 512
RATE = 1e-3
DECAY = 1e-4


@dataclass(frozen=True)
class Score:
    agreement: float  # share of decisions where the policy's best option is the bot's
    value_error: float  # mean squared error of the value
    value_accuracy: float  # share of won or lost battles whose winner the value calls right


def device() -> torch.device:
    """The Mac's GPU when there is one, else the CPU."""
    return torch.device("mps" if torch.backends.mps.is_available() else "cpu")


def load(paths: list[Path]) -> dict[str, npt.NDArray[np.generic]]:
    files = [np.load(path) for path in paths]
    return {key: np.concatenate([f[key] for f in files]) for key in files[0].files}


def _tensors(
    rows: dict[str, npt.NDArray[np.generic]], index: npt.NDArray[np.intp], on: torch.device
) -> tuple[torch.Tensor, ...]:
    return (
        torch.from_numpy(rows["pokemon"][index].astype(np.float32)).to(on),
        torch.from_numpy(rows["ids"][index].astype(np.int64)).to(on),
        torch.from_numpy(rows["field"][index]).to(on),
        torch.from_numpy(rows["mask"][index]).to(on),
        torch.from_numpy(rows["action"][index].astype(np.int64)).to(on),
        torch.from_numpy(rows["result"][index]).to(on),
    )


@torch.no_grad()
def score(
    net: PolicyValueNet, rows: dict[str, npt.NDArray[np.generic]], index: npt.NDArray[np.intp]
) -> Score:
    on = next(net.parameters()).device
    net.eval()
    agree = error = right = decided = 0.0
    for start in range(0, len(index), 4096):
        pokemon, ids, field, mask, action, result = _tensors(rows, index[start : start + 4096], on)
        logits, value = net(pokemon, ids, field, mask)
        agree += float((logits.argmax(-1) == action).sum())
        error += float(((value - result) ** 2).sum())
        won_or_lost = result != 0
        right += float(((value > 0) == (result > 0))[won_or_lost].sum())
        decided += float(won_or_lost.sum())
    return Score(agree / len(index), error / len(index), right / max(decided, 1.0))


def pretrain(
    rows: dict[str, npt.NDArray[np.generic]],
    *,
    epochs: int = EPOCHS,
    seed: int = 0,
    on: torch.device | None = None,
    report: Callable[[int, Score], None] | None = None,
) -> tuple[PolicyValueNet, Score, Score]:
    """Trains a new network on every battle but the test ones; its scores in training and test."""
    on = on or device()
    torch.manual_seed(seed)
    net = PolicyValueNet(Shape.for_catalog()).to(on)
    optimizer = torch.optim.Adam(net.parameters(), lr=RATE, weight_decay=DECAY)
    test = rows["seed"] % TEST_EVERY == 0
    train_index, test_index = np.flatnonzero(~test), np.flatnonzero(test)
    rng = np.random.default_rng(seed)
    for epoch in range(epochs):
        net.train()
        order = rng.permutation(train_index)
        for start in range(0, len(order), BATCH):
            pokemon, ids, field, mask, action, result = _tensors(
                rows, order[start : start + BATCH], on
            )
            logits, value = net(pokemon, ids, field, mask)
            loss = functional.cross_entropy(logits, action) + functional.mse_loss(value, result)
            optimizer.zero_grad()
            loss.backward()  # type: ignore[no-untyped-call]
            optimizer.step()
        if report is not None:
            report(epoch + 1, score(net, rows, test_index))
    net = net.cpu()
    return net, score(net, rows, train_index), score(net, rows, test_index)
