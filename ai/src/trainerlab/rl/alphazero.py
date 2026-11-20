"""The AlphaZero loop: rule 11 plays against itself, and the network learns from its search.

Each iteration plays `GAMES` battles with the current network, in C++ on every core
(search/src/self_play.cpp): both sides search what they see and play an option drawn in proportion
to its visits. A share `FULL_SHARE` of the searches are full (`SIMULATIONS`, noise at the root) and
leave the share of visits each option got and the battle's result for that side; the others, fast
(`FAST` simulations, no noise), only move the battle on (KataGo's playout cap randomization).
The network then learns, from the decisions of the last `WINDOW` iterations, to put its policy
where the search went (cross-entropy) and its value on the result (squared error). Guided by the
better network, the next search finds better moves still. Settings fixed before any measurement.

Needs PyTorch: pip install -e "ai/.[rl]".
"""

from collections.abc import Callable
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt
import torch
from torch.nn import functional

from trainerlab.rl.net import PolicyValueNet
from trainerlab.rl.planner import to_cpp

GAMES = 960  # battles per iteration: 6 rounds of 16 battles side by side on 10 threads
WINDOW = 4  # iterations whose decisions are learned from
EPOCHS = 2  # passes over the window per iteration
BATCH = 1024
RATE = 1e-4  # low: the loop starts from a network already trained (rule 10)
DECAY = 1e-4
SIMULATIONS = 800  # a full search, recorded
FAST = 100  # a fast search, unrecorded
FULL_SHARE = 0.25
WORLDS = 8
NOISE = 0.25

Rows = dict[str, npt.NDArray[np.generic]]
KEYS = ("pokemon", "ids", "field", "mask", "policy", "result")


@dataclass(frozen=True)
class Progress:
    iteration: int
    games: int  # battles played in this iteration
    decisions: int  # decisions recorded in this iteration
    turns: float  # mean turns per battle
    draws: float  # share of battles stopped at the turn limit or drawn
    entropy: float  # of the search's visits, per decision recorded: how decided the targets are
    policy_loss: float  # cross-entropy against the search's visits, on the last pass
    value_loss: float


def play(
    net: PolicyValueNet,
    games: int,
    *,
    seed: int,
    threads: int,
    simulations: int = SIMULATIONS,
    worlds: int = WORLDS,
    fast: int = FAST,
    share: float = FULL_SHARE,
) -> Rows:
    """`games` battles of rule 11 against itself with this network, and what they leave."""
    rows: Rows = to_cpp(net).self_play(
        games, seed, threads, simulations, worlds, NOISE, fast=fast, full_share=share
    )
    return rows


def entropy(policy: npt.NDArray[np.generic]) -> float:
    """The mean entropy (nats) of these rows of visit shares; 0 without rows."""
    p = np.asarray(policy, dtype=np.float64)
    if len(p) == 0:
        return 0.0
    logs = np.log(p, out=np.zeros_like(p), where=p > 0)
    return float(-(p * logs).sum() / len(p))


def learn(
    net: PolicyValueNet,
    optimizer: torch.optim.Optimizer,
    rows: Rows,
    *,
    epochs: int = EPOCHS,
    rng: np.random.Generator,
) -> tuple[float, float]:
    """`epochs` passes over `rows`; the mean policy and value losses of the last pass."""
    on = next(net.parameters()).device
    tensors = {
        key: torch.from_numpy(rows[key].astype(np.int64 if key == "ids" else np.float32))
        for key in KEYS
    }
    net.train()
    totals, count = np.zeros(2), 0
    for _ in range(epochs):
        totals, count = np.zeros(2), 0
        order = rng.permutation(len(rows["result"]))
        for start in range(0, len(order), BATCH):
            i = torch.from_numpy(order[start : start + BATCH])
            pokemon, ids, field, mask, policy, result = (tensors[key][i].to(on) for key in KEYS)
            logits, value = net(pokemon, ids, field, mask)
            policy_loss = -(policy * functional.log_softmax(logits, dim=-1)).sum(-1).mean()
            value_loss = functional.mse_loss(value, result)
            optimizer.zero_grad()
            (policy_loss + value_loss).backward()  # type: ignore[no-untyped-call]
            optimizer.step()
            totals += [policy_loss.item() * len(i), value_loss.item() * len(i)]
            count += len(i)
    net.eval()
    mean = totals / max(count, 1)
    return float(mean[0]), float(mean[1])


def loop(
    net: PolicyValueNet,
    iterations: int,
    *,
    seed: int = 0,
    threads: int = 1,
    games: int = GAMES,
    simulations: int = SIMULATIONS,
    worlds: int = WORLDS,
    fast: int = FAST,
    share: float = FULL_SHARE,
    on: torch.device | None = None,
    report: Callable[[Progress, PolicyValueNet], None] | None = None,
) -> PolicyValueNet:
    """Runs `iterations` of the loop on `net`; returns it, on the CPU."""
    on = on or torch.device("cpu")
    torch.manual_seed(seed)
    rng = np.random.default_rng(seed)
    net = net.to(on).eval()
    optimizer = torch.optim.Adam(net.parameters(), lr=RATE, weight_decay=DECAY)
    window: list[Rows] = []
    for iteration in range(1, iterations + 1):
        # Battle numbers seed' x 1000003 + g, far from the duels' seeds (below 1000003).
        played = play(
            net,
            games,
            seed=seed * 1000 + iteration,
            threads=threads,
            simulations=simulations,
            worlds=worlds,
            fast=fast,
            share=share,
        )
        window = [*window, played][-WINDOW:]
        rows = {key: np.concatenate([w[key] for w in window]) for key in KEYS}
        losses = learn(net, optimizer, rows, rng=rng)
        if report is not None:
            turns, winner = played["turns"], played["winner"].astype(np.int64)
            report(
                Progress(
                    iteration,
                    games,
                    len(played["result"]),
                    float(turns.mean()) if len(turns) else 0.0,
                    float((winner < 0).mean()) if len(winner) else 0.0,
                    entropy(played["policy"]),
                    *losses,
                ),
                net,
            )
    return net.cpu()
