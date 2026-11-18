"""The policy and value network: what to play, and how likely the side to play is to win.

It reads the encoding of learning/encoding.py. The same small network reads each of the twelve
Pokemon, from its numbers and the embeddings of its species, ability, item and four moves. The
trunk then reads both actives, the mean of each team and the field. Two heads come out of it:

- the policy, ten logits: each move from the trunk and that move (its embedding and numbers, on
  the active Pokemon), each switch from the trunk and the Pokemon it would send in; illegal
  options are masked out;
- the value, the expected result for the side to play, between -1 (a loss) and 1 (a win).

Needs PyTorch: pip install -e "ai/.[rl]".
"""

from dataclasses import asdict, dataclass
from pathlib import Path

import torch
from torch import nn

from trainerlab import _engine
from trainerlab.game.environment import DATA_DIR
from trainerlab.learning.encoding import ACTIONS, field_names, pokemon_names

MOVES = 4
TEAM = 6
MASKED = -1e9  # the logit of an illegal option


@dataclass(frozen=True)
class Shape:
    """What the network is built for: the sizes of its inputs and of its layers."""

    pokemon: int  # numbers per Pokemon
    field: int
    species: int  # catalog sizes, for the embeddings
    abilities: int
    items: int
    moves: int
    embedding: int = 16
    hidden: int = 128
    trunk: int = 256

    @classmethod
    def for_catalog(cls) -> "Shape":
        _engine.engine_init(str(DATA_DIR))
        return cls(
            pokemon=len(pokemon_names()),
            field=len(field_names()),
            species=_engine.species_count(),
            abilities=_engine.ability_count(),
            items=_engine.item_count(),
            moves=_engine.move_count(),
        )


class PolicyValueNet(nn.Module):
    def __init__(self, shape: Shape) -> None:
        super().__init__()
        self.shape = shape
        e, h = shape.embedding, shape.hidden
        # Index 0 is "none or unknown": ids are catalog index + 1.
        self.species = nn.Embedding(shape.species + 1, e, padding_idx=0)
        self.ability = nn.Embedding(shape.abilities + 1, e, padding_idx=0)
        self.item = nn.Embedding(shape.items + 1, e, padding_idx=0)
        self.move = nn.Embedding(shape.moves + 1, e, padding_idx=0)
        self.pokemon = nn.Sequential(
            nn.Linear(shape.pokemon + 7 * e, h), nn.ReLU(), nn.Linear(h, h), nn.ReLU()
        )
        self.trunk = nn.Sequential(
            nn.Linear(4 * h + shape.field, shape.trunk),
            nn.ReLU(),
            nn.Linear(shape.trunk, shape.trunk),
            nn.ReLU(),
        )
        names = pokemon_names()
        self._present = names.index("present")
        self._active = names.index("active")
        self._move_columns = names.index("move1_present")  # four blocks of the same width
        self._move_width = names.index("move2_present") - self._move_columns
        self.moves = nn.Sequential(
            nn.Linear(shape.trunk + e + self._move_width, h), nn.ReLU(), nn.Linear(h, 1)
        )
        self.switch = nn.Sequential(nn.Linear(shape.trunk + h, h), nn.ReLU(), nn.Linear(h, 1))
        self.value = nn.Linear(shape.trunk, 1)

    def forward(
        self, pokemon: torch.Tensor, ids: torch.Tensor, field: torch.Tensor, mask: torch.Tensor
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """pokemon[B, 12, P], ids[B, 12, 7], field[B, F], mask[B, 10] -> logits[B, 10], value[B]."""
        embedded = torch.cat(
            [
                self.species(ids[..., 0]),
                self.ability(ids[..., 1]),
                self.item(ids[..., 2]),
                self.move(ids[..., 3:]).flatten(-2),
            ],
            dim=-1,
        )
        each = self.pokemon(torch.cat([pokemon, embedded], dim=-1))  # B x 12 x H
        active = pokemon[..., self._active].unsqueeze(-1)
        present = pokemon[..., self._present].unsqueeze(-1)
        mine, theirs = slice(0, TEAM), slice(TEAM, 2 * TEAM)

        def side_mean(part: slice) -> torch.Tensor:
            weight = present[:, part]
            mean: torch.Tensor = (each[:, part] * weight).sum(1) / weight.sum(1).clamp(min=1.0)
            return mean

        trunk = self.trunk(
            torch.cat(
                [
                    (each[:, mine] * active[:, mine]).sum(1),
                    (each[:, theirs] * active[:, theirs]).sum(1),
                    side_mean(mine),
                    side_mean(theirs),
                    field,
                ],
                dim=-1,
            )
        )
        switches = self.switch(
            torch.cat([trunk.unsqueeze(1).expand(-1, TEAM, -1), each[:, mine]], dim=-1)
        ).squeeze(-1)
        # The active Pokemon's four moves, each read with the trunk.
        lead = active[:, mine, 0].argmax(1)
        rows = torch.arange(len(lead), device=lead.device)
        start, width = self._move_columns, self._move_width
        numbers = pokemon[rows, lead, start : start + MOVES * width].view(-1, MOVES, width)
        moves = torch.cat(
            [trunk.unsqueeze(1).expand(-1, MOVES, -1), self.move(ids[rows, lead, 3:]), numbers],
            dim=-1,
        )
        logits = torch.cat([self.moves(moves).squeeze(-1), switches], dim=-1)
        logits = logits.masked_fill(mask == 0, MASKED)
        return logits, torch.tanh(self.value(trunk)).squeeze(-1)

    def save(self, path: Path) -> None:
        torch.save({"shape": asdict(self.shape), "weights": self.state_dict()}, path)

    @classmethod
    def load(cls, path: Path) -> "PolicyValueNet":
        saved = torch.load(path, map_location="cpu", weights_only=True)
        net = cls(Shape(**saved["shape"]))
        net.load_state_dict(saved["weights"])
        net.eval()
        return net


assert ACTIONS == MOVES + TEAM
