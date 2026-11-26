"""Rule 11: a tree search over simultaneous moves, guided by the policy and value network.

The search runs in C++ (search/src/planner.cpp), and so does the network
(search/src/network.cpp), with the weights of a network trained here. Needs PyTorch to read them:
pip install -e "ai/.[rl]".
"""

import random
from pathlib import Path

import numpy as np

from trainerlab import _engine, _search
from trainerlab.game import leads
from trainerlab.game.building import random_builder, species
from trainerlab.game.environment import DATA_DIR, BattleEnvironment
from trainerlab.learning.encoding import ACTIONS
from trainerlab.rl.net import PolicyValueNet

MOVES = 4


def cpp_network(path: Path) -> _search.Network:
    """The network in this file, evaluated in C++."""
    return to_cpp(PolicyValueNet.load(path))


def to_cpp(net: PolicyValueNet) -> _search.Network:
    """This network, wherever it lives, copied to C++."""
    _engine.engine_init(str(DATA_DIR))
    _search.init(str(DATA_DIR))
    weights = net.state_dict()
    return _search.Network({name: t.detach().cpu().numpy() for name, t in weights.items()})


SIMULATIONS = 200  # rule 11's, fixed before measuring
# Palier 10.4: a replacement searched searches each option a quarter of a decision's simulations.
REPLACEMENT_SHARE = 4


class Planner:
    """Searches `simulations` simulations per decision (search/include/search/planner.hpp), and
    plays the option visited most. Replacements are the network's favourite switch, or with
    `replacements` the option whose position a short search values most (palier 10.4)."""

    def __init__(
        self,
        path: Path,
        *,
        seed: int = 0,
        simulations: int = SIMULATIONS,
        replacements: bool = False,
    ) -> None:
        self.network = cpp_network(path)
        self._rng = random.Random(seed)
        self._simulations = simulations
        self._replacements = replacements

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        seed = self._rng.getrandbits(63)
        return legal[self.network.plan_action(view, side, legal, seed, self._simulations)]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        if self._replacements and len(options) > 1:  # palier 10.4: each option searched
            seed = self._rng.getrandbits(63)
            simulations = max(2, self._simulations // REPLACEMENT_SHARE)
            return self.network.plan_replacement(view, side, options, seed, simulations)
        mask = np.zeros(ACTIONS, dtype=np.float32)
        mask[[MOVES + index for index in options]] = 1
        logits, _ = self.network.evaluate(view, side, mask)
        return int(np.argmax(logits)) - MOVES


def lead_view(team: list[str], lead: int, foe: str) -> _engine.BattleState:
    """What side 0 sees as the battle starts, its team led by `team[lead]` and the foe's by
    `foe`: the rest of the foe's team, as many Pokemon, is hidden (any legal ones)."""
    mine = [team[lead], *team[:lead], *team[lead + 1 :]]
    theirs = random_builder([foe], len(team), 0, species())
    return BattleEnvironment(mine, theirs, seed=0).observe(0)


class LeadChooser:
    """A search bot that chooses its lead (palier 13.1): each of its Pokemon is worth the
    network's value of the start of the battle against each foe lead, weighed by the chance
    that the foe leads with it (game/leads.py); the best leads. Plays as the bot it wraps."""

    def __init__(self, bot: Planner) -> None:
        self.bot, self.network = bot, bot.network

    def lead_values(self, team: list[str]) -> list[float]:
        every = np.ones(ACTIONS, dtype=np.float32)
        chances = leads.foe_leads()
        return [
            sum(
                p * self.network.evaluate(lead_view(team, k, foe), 0, every)[1]
                for foe, p in chances.items()
            )
            for k in range(len(team))
        ]

    def choose_lead(self, team: list[str]) -> int:
        if len(team) == 1:
            return 0
        return int(np.argmax(self.lead_values(team)))

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        return self.bot.choose_action(side, view, legal)

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        return self.bot.choose_replacement(side, view, options)
