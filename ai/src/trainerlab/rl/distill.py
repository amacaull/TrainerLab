"""Palier 10: a new network learns what an old one's search chooses (distillation).

The teacher (rule 13's network) plays the AlphaZero loop's battles, with the loop's settings
(rl/alphazero.py: a quarter of the searches deep and recorded), until it has left `ROWS`
decisions. The student, a network with layers of attention (rl/net.py), then learns them as the
loop learns: its policy toward the search's visits, its value toward the results. The last
`HELD` of the decisions, from the last battles, are kept apart to see that it generalises. The
AlphaZero loop then goes on from the student. Settings fixed before any measurement.

Needs PyTorch: pip install -e "ai/.[rl]".
"""

import math
from collections.abc import Callable
from dataclasses import asdict, dataclass
from typing import Any

import numpy as np
import torch
from torch.nn import functional

from trainerlab.rl import alphazero, train
from trainerlab.rl.alphazero import KEYS, Rows
from trainerlab.rl.net import PolicyValueNet, Shape

ROWS = 500_000  # decisions the teacher leaves
LAYERS = 2  # of attention in the student
EPOCHS = 8
BATCH = 1024
RATE = 1e-3  # at the start, falling along a cosine to FLOOR at the end
FLOOR = 1e-5
DECAY = 1e-4
HELD = 0.05  # of the decisions, the last ones, kept apart
SEED = 10_000  # battle numbers (SEED + round) x 1000003 + g: apart from the loop's and the duels'
GENERATION: dict[str, Any] = {}  # the loop's settings (the tests play smaller)


@dataclass(frozen=True)
class Epoch:
    epoch: int
    policy: float  # mean losses of the pass, on the decisions learned from
    value: float
    held_policy: float  # on the decisions kept apart
    held_value: float


def generate(
    teacher: PolicyValueNet,
    rows: int,
    *,
    threads: int,
    report: Callable[[int, int], None] | None = None,
    games: int = alphazero.GAMES,
    **settings: Any,
) -> Rows:
    """Rounds of `games` battles of the teacher's search against itself, until `rows` decisions."""
    chunks: list[Rows] = []
    total = 0
    while total < rows:
        played = alphazero.play(
            teacher, games, seed=SEED + len(chunks), threads=threads, **settings
        )
        chunks.append({key: played[key] for key in KEYS})
        total += len(played["result"])
        if report is not None:
            report(len(chunks), total)
    return {key: np.concatenate([chunk.pop(key) for chunk in chunks]) for key in KEYS}


def student(layers: int = LAYERS) -> PolicyValueNet:
    """A fresh network with `layers` of attention, for the catalog."""
    return PolicyValueNet(Shape(**{**asdict(Shape.for_catalog()), "layers": layers}))


def _batch(rows: Rows, index: np.ndarray, on: torch.device) -> list[torch.Tensor]:
    return [
        torch.from_numpy(rows[key][index].astype(np.int64 if key == "ids" else np.float32)).to(on)
        for key in KEYS
    ]


def _loss(net: PolicyValueNet, batch: list[torch.Tensor]) -> tuple[torch.Tensor, torch.Tensor]:
    pokemon, ids, field, mask, policy, result = batch
    logits, value = net(pokemon, ids, field, mask)
    policy_loss = -(policy * functional.log_softmax(logits, dim=-1)).sum(-1).mean()
    return policy_loss, functional.mse_loss(value, result)


def losses(net: PolicyValueNet, rows: Rows, *, batch: int = BATCH) -> tuple[float, float]:
    """The network's mean policy cross-entropy and value squared error on these decisions."""
    on = next(net.parameters()).device
    totals, n = np.zeros(2), len(rows["result"])
    net.eval()
    with torch.no_grad():
        for start in range(0, n, batch):
            index = np.arange(start, min(start + batch, n))
            policy_loss, value_loss = _loss(net, _batch(rows, index, on))
            totals += [policy_loss.item() * len(index), value_loss.item() * len(index)]
    mean = totals / max(n, 1)
    return float(mean[0]), float(mean[1])


def rate(step: int, steps: int) -> float:
    """The learning rate at `step` of `steps`: RATE down to FLOOR along half a cosine."""
    return FLOOR + (RATE - FLOOR) * 0.5 * (1 + math.cos(math.pi * step / max(steps, 1)))


def teach(
    net: PolicyValueNet,
    rows: Rows,
    *,
    epochs: int = EPOCHS,
    report: Callable[[Epoch], None] = lambda _: None,
    batch: int = BATCH,
    seed: int = 0,
) -> PolicyValueNet:
    """`epochs` passes of the student over all but the last `HELD` of `rows`; it, on the CPU."""
    n = len(rows["result"])
    learned = max(1, int(n * (1 - HELD)))
    held = {key: rows[key][learned:] for key in KEYS}
    on = train.device()
    net.to(on)
    optimizer = torch.optim.Adam(net.parameters(), lr=RATE, weight_decay=DECAY)
    steps = epochs * math.ceil(learned / batch)
    schedule = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda s: rate(s, steps) / RATE)
    rng = np.random.default_rng(seed)
    for epoch in range(1, epochs + 1):
        net.train()
        totals, count = np.zeros(2), 0
        order = rng.permutation(learned)
        for start in range(0, learned, batch):
            index = np.sort(order[start : start + batch])
            policy_loss, value_loss = _loss(net, _batch(rows, index, on))
            optimizer.zero_grad()
            (policy_loss + value_loss).backward()  # type: ignore[no-untyped-call]
            optimizer.step()
            schedule.step()
            totals += [policy_loss.item() * len(index), value_loss.item() * len(index)]
            count += len(index)
        mean = totals / count
        report(Epoch(epoch, float(mean[0]), float(mean[1]), *losses(net, held, batch=batch)))
    return net.cpu().eval()
