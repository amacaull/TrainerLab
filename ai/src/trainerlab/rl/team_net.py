"""The strength of a team (palier 12.4): a number per team, from its species alone.

Two teams' chances follow from their strengths, as in an Elo or Bradley-Terry model: team A beats
team B with probability sigmoid(v_A - v_B). The builder never sees the foe's team, so what it
needs is a team's worth in general, one number, not a matchup.

Two models give it:

- Additive: a weight per species and team size, summed over the team: what a ranking of the
  species knows (the species builder), nothing of how they go together;
- TeamNet: each Pokemon an embedding of its species, plus one of the team's size; two layers of
  attention, each Pokemon reading its teammates (a weather setter and who uses the weather, a
  hazard setter and who profits); their mean, then a small head, one number. No position: the
  order of the team changes nothing.

A team is a row of six engine species ids, PAD after its last Pokemon.

Needs PyTorch: pip install -e "ai/.[rl]".
"""

from pathlib import Path

import numpy as np
import torch
from torch import nn

SPECIES = 49  # the engine's catalog
PAD = SPECIES  # an empty place in a team
TEAM = 6
WIDTH = 64  # of each Pokemon's vector
LAYERS = 2
HEADS = 4
FEEDFORWARD = 128


def _sizes(species: torch.Tensor) -> torch.Tensor:
    return (species != PAD).sum(dim=1)


class Additive(nn.Module):
    """A weight per (team size, species), summed over the team."""

    kind = "additive"

    def __init__(self) -> None:
        super().__init__()
        self.weights = nn.Parameter(torch.zeros(TEAM + 1, SPECIES + 1))

    def forward(self, species: torch.Tensor) -> torch.Tensor:
        rows = self.weights[_sizes(species)]  # [N, SPECIES + 1]
        worth = torch.gather(rows, 1, species) * (species != PAD)
        return worth.sum(dim=1)

    def save(self, path: Path) -> None:
        torch.save({"kind": self.kind, "state": self.state_dict()}, path)


class TeamNet(nn.Module):
    """Attention over the team's Pokemon, then their mean, then one number."""

    kind = "attention"

    def __init__(self) -> None:
        super().__init__()
        self.species = nn.Embedding(SPECIES + 1, WIDTH, padding_idx=PAD)
        self.size = nn.Embedding(TEAM + 1, WIDTH)
        layer = nn.TransformerEncoderLayer(WIDTH, HEADS, FEEDFORWARD, dropout=0.0, batch_first=True)
        self.attention = nn.TransformerEncoder(layer, LAYERS, enable_nested_tensor=False)
        self.head = nn.Sequential(nn.Linear(WIDTH, WIDTH), nn.ReLU(), nn.Linear(WIDTH, 1))

    def forward(self, species: torch.Tensor) -> torch.Tensor:
        empty = species == PAD
        tokens = self.species(species) + self.size(_sizes(species))[:, None, :]
        read = self.attention(tokens, src_key_padding_mask=empty)
        kept = (~empty).unsqueeze(-1).float()
        mean = (read * kept).sum(dim=1) / kept.sum(dim=1)
        strength: torch.Tensor = self.head(mean).squeeze(-1)
        return strength

    def save(self, path: Path) -> None:
        torch.save({"kind": self.kind, "state": self.state_dict()}, path)


def load(path: Path) -> nn.Module:
    """A model kept by save()."""
    kept = torch.load(path, map_location="cpu")
    model: nn.Module = TeamNet() if kept["kind"] == TeamNet.kind else Additive()
    model.load_state_dict(kept["state"])
    return model


def win_probability(a: torch.Tensor, b: torch.Tensor) -> torch.Tensor:
    """The chance that the team of strength `a` beats the one of strength `b`."""
    return torch.sigmoid(a - b)


def log_loss(p: torch.Tensor, y: torch.Tensor) -> float:
    """The mean log loss of chances `p` against points `y` (1 a win, 0.5 a draw, 0 a loss)."""
    eps = 1e-7
    q = p.clamp(eps, 1 - eps)
    return float(-(y * torch.log(q) + (1 - y) * torch.log(1 - q)).mean())


def fit(
    model: nn.Module,
    a: torch.Tensor,
    b: torch.Tensor,
    y: torch.Tensor,
    *,
    epochs: int,
    batch: int,
    rate: float,
    seed: int = 0,
) -> list[float]:
    """Learns from battles: team a against team b, a's points y. The mean loss of each epoch."""
    optimizer = torch.optim.Adam(model.parameters(), lr=rate)
    order = np.random.default_rng(seed)
    losses = []
    model.train()
    for _ in range(epochs):
        total = 0.0
        shuffled = torch.from_numpy(order.permutation(len(y)))
        for start in range(0, len(y), batch):
            rows = shuffled[start : start + batch]
            p = win_probability(model(a[rows]), model(b[rows]))
            loss = nn.functional.binary_cross_entropy(p.clamp(1e-7, 1 - 1e-7), y[rows])
            optimizer.zero_grad()
            loss.backward()  # type: ignore[no-untyped-call]
            optimizer.step()
            total += float(loss.detach()) * len(rows)
        losses.append(total / len(y))
    model.eval()
    return losses
