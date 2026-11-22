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
from typing import Any

import numpy as np
import numpy.typing as npt
import torch
from torch.nn import functional

from trainerlab.rl.net import PolicyValueNet
from trainerlab.rl.planner import to_cpp

GAMES = 960  # battles per iteration: 6 rounds of 16 battles side by side on 10 threads
WINDOW = 4  # iterations whose decisions are learned from
# Palier 10.2: one pass over the window per iteration (two before), and the value's loss weighed
# 0.25 (1 before), as Leela Chess Zero does: a network with attention learnt the battles' results
# by heart when each decision came back eight times, and its search drifted.
EPOCHS = 1  # passes over the window per iteration
BATCH = 1024
# 3e-4 the first half (then 1e-4, 3e-5) made the network with attention worse on battles it had
# not learned from, its value learning them by heart again. Palier 10.5: 1e-4, then 3e-5 the last
# quarter of the iterations, to settle.
RATES = ((0.75, 1e-4), (1.0, 3e-5))
DECAY = 1e-4
SIMULATIONS = 800  # a full search, recorded
FAST = 100  # a fast search, unrecorded
FULL_SHARE = 0.25
WORLDS = 8
NOISE = 0.25

Rows = dict[str, npt.NDArray[np.generic]]
KEYS = ("pokemon", "ids", "field", "mask", "policy", "result", "foe", "foe_mask", "search_value")
# Palier 10.2: the value learns the mean of the battle's result and the search's value of the
# position: the result alone is noisy (the dice), the search's value averages hundreds of lines.
SEARCH_SHARE = 0.5
# Palier 10.3: a quarter of each iteration's battles start from the endgames (3 Pokemon or fewer
# standing a side) the battles of the iteration before reached, with dice of their own: the
# network sees many more of the long endgames it judged worst (palier 7).
# Palier 10.5: off, the battles with small teams below taking their place.
ENDGAME_SHARE = 0.0
# Palier 10.5: half the battles 6 against 6, a quarter 4 against 4, a quarter 2 against 2: whole
# battles, with more of the knock-outs and endgames the network judged worst.
TEAM_SIZES = ((6, 0.5), (4, 0.25), (2, 0.25))
# Palier 10.4: after a knock-out with a choice, each option searched this many simulations and the
# best sent in, a row to learn from (the network never learnt its replacements before).
REPLACEMENTS = 50
VALUE_WEIGHT = 0.25  # of the value's loss, beside the policy's
FOE_WEIGHT = 0.5  # of the foe head's loss, beside the policy's and the value's (palier 10.2)


@dataclass(frozen=True)
class Progress:
    iteration: int
    games: int  # battles played in this iteration
    decisions: int  # decisions recorded in this iteration
    turns: float  # mean turns per battle
    draws: float  # share of battles stopped at the turn limit or drawn
    entropy: float  # of the search's visits, per decision recorded: how decided the targets are
    fresh_policy: float  # the losses on this iteration's decisions, before learning them: on
    fresh_value: float  # battles the network has not learned from (learning by heart shows here)
    policy_loss: float  # cross-entropy against the search's visits, on the last pass
    value_loss: float
    from_endgames: int = 0  # battles started from an endgame of the iteration before
    small_teams: int = 0  # battles with teams of fewer than 6


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
    endgames: list[Any] | None = None,
    replacements: int = REPLACEMENTS,
) -> Rows:
    """`games` battles of rule 11 against itself with this network, and what they leave; given
    endgames, a share ENDGAME_SHARE of the battles start from one of them; teams sized as
    TEAM_SIZES."""
    rows: Rows = to_cpp(net).self_play(
        games,
        seed,
        threads,
        simulations,
        worlds,
        NOISE,
        fast=fast,
        full_share=share,
        endgames=endgames or [],
        endgame_share=ENDGAME_SHARE,
        replacement_simulations=replacements,
        team_sizes=list(TEAM_SIZES),
    )
    return rows


def entropy(policy: npt.NDArray[np.generic]) -> float:
    """The mean entropy (nats) of these rows of visit shares; 0 without rows."""
    p = np.asarray(policy, dtype=np.float64)
    if len(p) == 0:
        return 0.0
    logs = np.log(p, out=np.zeros_like(p), where=p > 0)
    return float(-(p * logs).sum() / len(p))


def _dtype(key: str) -> type:
    return np.int64 if key in ("ids", "foe") else np.float32


def losses(
    net: PolicyValueNet, batch: list[torch.Tensor], *, search_share: float = SEARCH_SHARE
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """On a batch (KEYS' tensors): the policy's cross-entropy against the search's visits, the
    value's squared error against the result mixed with the search's value (`search_share` of
    it), and with a foe head its cross-entropy against what the foe played."""
    pokemon, ids, field, mask, policy, result, foe, foe_mask, searched = batch
    target = (1 - search_share) * result + search_share * searched
    logits, value, guess = net.evaluate(pokemon, ids, field, mask, foe_mask)
    policy_loss = -(policy * functional.log_softmax(logits, dim=-1)).sum(-1).mean()
    known = foe >= 0  # a replacement has no foe's option to guess
    if guess is not None and bool(known.any()):
        foe_loss = functional.cross_entropy(guess[known], foe[known])
    else:
        foe_loss = logits.new_zeros(())
    return policy_loss, functional.mse_loss(value, target), foe_loss


def measure(net: PolicyValueNet, rows: Rows, *, batch: int = BATCH) -> tuple[float, float]:
    """The network's mean policy cross-entropy and value squared error on these decisions, the
    value against the results alone: comparable from one loop to the next."""
    on = next(net.parameters()).device
    totals, n = np.zeros(2), len(rows["result"])
    net.eval()
    with torch.no_grad():
        for start in range(0, n, batch):
            part = slice(start, min(start + batch, n))
            tensors = [torch.from_numpy(rows[key][part].astype(_dtype(key))).to(on) for key in KEYS]
            policy_loss, value_loss, _ = losses(net, tensors, search_share=0.0)
            totals += [
                policy_loss.item() * (part.stop - start),
                value_loss.item() * (part.stop - start),
            ]
    mean = totals / max(n, 1)
    return float(mean[0]), float(mean[1])


def foe_loss(net: PolicyValueNet, rows: Rows) -> float:
    """The foe head's cross-entropy on these decisions (0 without a foe head)."""
    with torch.no_grad():
        on = next(net.parameters()).device
        batch = [torch.from_numpy(rows[key].astype(_dtype(key))).to(on) for key in KEYS]
        return float(losses(net, batch)[2])


def rate(iteration: int, iterations: int) -> float:
    """The learning rate of iteration `iteration` (from 1) of `iterations`."""
    done = (iteration - 1) / iterations
    return next(value for until, value in RATES if done < until)


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
    tensors = {key: torch.from_numpy(rows[key].astype(_dtype(key))) for key in KEYS}
    net.train()
    totals, count = np.zeros(2), 0
    for _ in range(epochs):
        totals, count = np.zeros(2), 0
        order = rng.permutation(len(rows["result"]))
        for start in range(0, len(order), BATCH):
            i = torch.from_numpy(order[start : start + BATCH])
            policy_loss, value_loss, foe = losses(net, [tensors[key][i].to(on) for key in KEYS])
            optimizer.zero_grad()
            loss = policy_loss + VALUE_WEIGHT * value_loss + FOE_WEIGHT * foe
            loss.backward()  # type: ignore[no-untyped-call]
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
    replacements: int = REPLACEMENTS,
    on: torch.device | None = None,
    report: Callable[[Progress, PolicyValueNet], None] | None = None,
) -> PolicyValueNet:
    """Runs `iterations` of the loop on `net`; returns it, on the CPU."""
    on = on or torch.device("cpu")
    torch.manual_seed(seed)
    rng = np.random.default_rng(seed)
    net = net.to(on).eval()
    optimizer = torch.optim.Adam(
        net.parameters(), lr=rate(1, max(iterations, 1)), weight_decay=DECAY
    )
    window: list[Rows] = []
    endgames: list[Any] = []  # those the iteration before reached
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
            endgames=endgames,
            replacements=replacements,
        )
        endgames = list(played["endgames"])
        fresh = measure(net, played)
        for group in optimizer.param_groups:
            group["lr"] = rate(iteration, iterations)
        window = [*window, played][-WINDOW:]
        rows = {key: np.concatenate([w[key] for w in window]) for key in KEYS}
        learned = learn(net, optimizer, rows, rng=rng)
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
                    *fresh,
                    *learned,
                    int(played["from_endgame"].astype(np.int64).sum()),
                    int((played["team_size"].astype(np.int64) < 6).sum()),
                ),
                net,
            )
    return net.cpu()
