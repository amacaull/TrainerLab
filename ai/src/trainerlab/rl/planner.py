"""Rule 11: a tree search over simultaneous moves, guided by the policy and value network.

The search runs in C++ (search/src/planner.cpp), and so does the network
(search/src/network.cpp), with the weights of a network trained here. Needs PyTorch to read them:
pip install -e "ai/.[rl]".
"""

import random
from pathlib import Path

import numpy as np

from trainerlab import _engine, _search
from trainerlab.game.environment import DATA_DIR
from trainerlab.learning.encoding import ACTIONS
from trainerlab.rl.net import PolicyValueNet

MOVES = 4


def cpp_network(path: Path) -> _search.Network:
    """The network in this file, evaluated in C++."""
    _engine.engine_init(str(DATA_DIR))
    _search.init(str(DATA_DIR))
    weights = PolicyValueNet.load(path).state_dict()
    return _search.Network({name: tensor.numpy() for name, tensor in weights.items()})


class Planner:
    """Searches 200 simulations per decision (search/include/search/planner.hpp), and plays the
    option visited most. Replacements are the network's favourite switch, without search."""

    def __init__(self, path: Path, *, seed: int = 0) -> None:
        self.network = cpp_network(path)
        self._rng = random.Random(seed)

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        return legal[self.network.plan_action(view, side, legal, self._rng.getrandbits(63))]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        mask = np.zeros(ACTIONS, dtype=np.float32)
        mask[[MOVES + index for index in options]] = 1
        logits, _ = self.network.evaluate(view, side, mask)
        return int(np.argmax(logits)) - MOVES
