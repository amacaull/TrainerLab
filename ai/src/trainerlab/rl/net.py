"""The policy and value network: what to play, and how likely the side to play is to win.

It reads the encoding of learning/encoding.py. The same small network reads each of the twelve
Pokemon, from its numbers and the embeddings of its species, ability, item and four moves. The
trunk then reads both actives, the mean of each team and the field. Two heads come out of it:

- the policy, ten logits: each move from the trunk and that move (its embedding and numbers, on
  the active Pokemon), each switch from the trunk and the Pokemon it would send in; illegal
  options are masked out;
- the value, the expected result for the side to play, between -1 (a loss) and 1 (a win).

With `layers` (palier 10), the twelve Pokemon and the field then go through that many layers of
attention before the trunk: each Pokemon reads the others (who threatens whom, who could come in),
and the trunk and the switches read what they made of each other.

Needs PyTorch: pip install -e "ai/.[rl]".
"""

import math
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
HEADS = 4  # of each layer of attention
TOKENS = 2 * TEAM + 1  # the twelve Pokemon and the field


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
    layers: int = 0  # of attention over the Pokemon and the field (palier 10)
    feedforward: int = 256  # each layer's
    foe_head: bool = False  # a head guessing the foe's option, learned beside (palier 10.2)

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


class Attention(nn.Module):
    """One layer of attention over the tokens, as in a transformer (normalised first): each token
    adds what it reads from all of them, then what a small network makes of it."""

    def __init__(self, width: int, feedforward: int) -> None:
        super().__init__()
        self.norm1 = nn.LayerNorm(width)
        self.qkv = nn.Linear(width, 3 * width)
        self.out = nn.Linear(width, width)
        self.norm2 = nn.LayerNorm(width)
        self.ffn = nn.Sequential(
            nn.Linear(width, feedforward), nn.ReLU(), nn.Linear(feedforward, width)
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        b, t, w = x.shape
        d = w // HEADS
        q, k, v = self.qkv(self.norm1(x)).view(b, t, 3, HEADS, d).permute(2, 0, 3, 1, 4)
        weights = torch.softmax(q @ k.transpose(-1, -2) / math.sqrt(d), dim=-1)
        x = x + self.out((weights @ v).transpose(1, 2).reshape(b, t, w))
        out: torch.Tensor = x + self.ffn(self.norm2(x))
        return out


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
        if shape.layers:
            self.field_token = nn.Linear(shape.field, h)
            self.slots = nn.Parameter(torch.randn(TOKENS, h) * 0.02)  # which place, which side
            self.attention = nn.ModuleList(
                [Attention(h, shape.feedforward) for _ in range(shape.layers)]
            )
            self.norm = nn.LayerNorm(h)
        field_width = h if shape.layers else shape.field
        self.trunk = nn.Sequential(
            nn.Linear(4 * h + field_width, shape.trunk),
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
        if shape.foe_head:
            self.foe = nn.Linear(shape.trunk, ACTIONS)

    def pokemon_tokens(
        self, pokemon: torch.Tensor, ids: torch.Tensor, field: torch.Tensor
    ) -> torch.Tensor:
        """What the network makes of each Pokemon, after attention if any: B x 12 x H."""
        return self._read(pokemon, ids, field)[0]

    def _read(
        self, pokemon: torch.Tensor, ids: torch.Tensor, field: torch.Tensor
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """Each Pokemon (B x 12 x H) and the field (B x F, or B x H after attention)."""
        pokemon = pokemon[..., : self.shape.pokemon]  # a network made before the matchup columns
        embedded = torch.cat(
            [
                self.species(ids[..., 0]),
                self.ability(ids[..., 1]),
                self.item(ids[..., 2]),
                self.move(ids[..., 3:]).flatten(-2),
            ],
            dim=-1,
        )
        each: torch.Tensor = self.pokemon(torch.cat([pokemon, embedded], dim=-1))
        if not self.shape.layers:
            return each, field
        tokens = torch.cat([each, self.field_token(field).unsqueeze(1)], dim=1) + self.slots
        for layer in self.attention:
            tokens = layer(tokens)
        tokens = self.norm(tokens)
        return tokens[:, : 2 * TEAM], tokens[:, 2 * TEAM]

    def forward(
        self, pokemon: torch.Tensor, ids: torch.Tensor, field: torch.Tensor, mask: torch.Tensor
    ) -> tuple[torch.Tensor, torch.Tensor]:
        """pokemon[B, 12, P], ids[B, 12, 7], field[B, F], mask[B, 10] -> logits[B, 10], value[B]."""
        logits, value, _ = self.evaluate(pokemon, ids, field, mask)
        return logits, value

    def evaluate(
        self,
        pokemon: torch.Tensor,
        ids: torch.Tensor,
        field: torch.Tensor,
        mask: torch.Tensor,
        foe_mask: torch.Tensor | None = None,
    ) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor | None]:
        """As forward, and with a foe head, its logits over the foe's options in foe_mask."""
        each, field_part = self._read(pokemon, ids, field)
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
                    field_part,
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
        value = torch.tanh(self.value(trunk)).squeeze(-1)
        if not self.shape.foe_head:
            return logits, value, None
        foe: torch.Tensor = self.foe(trunk)
        if foe_mask is not None:
            foe = foe.masked_fill(foe_mask == 0, MASKED)
        return logits, value, foe

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
