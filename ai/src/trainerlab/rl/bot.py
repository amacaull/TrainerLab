"""A bot that plays what the policy network rates best, without any search.

Needs PyTorch: pip install -e "ai/.[rl]".
"""

from pathlib import Path

import numpy as np
import torch

from trainerlab import _engine
from trainerlab.learning.encoding import ACTIONS, action_index, action_mask, encode
from trainerlab.rl.net import PolicyValueNet

MOVES = 4


class NetBot:
    """Plays the legal option with the highest logit: a deterministic, greedy policy."""

    def __init__(self, path: Path) -> None:
        torch.set_num_threads(1)  # one process per core already: no threads on top
        self.net = PolicyValueNet.load(path)

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        best = self._best(view, side, action_mask(legal))
        return next(a for a in legal if action_index(a) == best)

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        mask = np.zeros(ACTIONS, dtype=np.float32)
        mask[[MOVES + index for index in options]] = 1
        return self._best(view, side, mask) - MOVES

    @torch.no_grad()
    def _best(self, view: _engine.BattleState, side: int, mask: np.ndarray) -> int:
        e = encode(view, side)
        logits, _ = self.net(
            torch.from_numpy(e.pokemon)[None],
            torch.from_numpy(e.ids).long()[None],
            torch.from_numpy(e.field)[None],
            torch.from_numpy(mask)[None],
        )
        return int(logits[0].argmax())
