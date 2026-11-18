"""Rules 6 to 8: look one turn ahead, and play what the win probability model rates best.

For each of its options, the bot plays the turn on a copy of what it sees, the foe playing what
rule 5 would play, then asks the model how likely it is to win from the position reached. A
replacement is chosen the same way, without a turn. The search runs in C++ (search/src/
forecaster.cpp); the Python version it replaced, decision for decision, is kept by the tests as
the reference (tests/bots/reference_lookahead.py).

The model ships with the package (learning/models/win-logistic.npz): the logistic regression
measured in docs/results.md, rebuilt by

    trainerlab dataset calculator survivor --pairs 2000 --from 40000
    trainerlab train calculator-vs-survivor-40000-41999.npz \\
      --out ai/src/trainerlab/learning/models/win-logistic.npz
"""

import random
from functools import cache

from trainerlab import _engine, _search
from trainerlab.game.environment import DATA_DIR
from trainerlab.learning.features import feature_names
from trainerlab.learning.model import SHIPPED, WinModel


class Forecaster:
    """Rule 6: plays each option a turn ahead, the foe playing as rule 5, and keeps the option
    whose position the model rates best."""

    def __init__(self, model: WinModel | None = None) -> None:
        self.model = model if model is not None else _shipped_model()
        if self.model.feature_names != feature_names():
            raise ValueError("the model was trained on other features: train it again")
        _search.init(str(DATA_DIR))
        self._search = _search.Forecaster(
            list(self.model.weights), list(self.model.biases), self.model.mean, self.model.std
        )

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        return legal[self._search.choose_action(view, side, legal)]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        return options[self._search.choose_replacement(view, side, options)]

    def odds(self, state: _engine.BattleState, side: int) -> float:
        """The probability of winning from this position, as the model sees it."""
        return self._search.odds(state, side)


class Strategist(Forecaster):
    """Rule 7: rule 6, but without guessing the foe: each option is rated by the foe's best reply,
    the one that leaves the bot the lowest odds (minimax, one turn deep).

    The replies are the foe's moves and its switches to Pokemon already seen: a Pokemon not seen
    yet is only a stand-in, nothing to reason on. An option is dropped as soon as one reply puts
    it below the best option so far: its worst case can only be lower.
    """

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        return legal[self._search.choose_strategist_action(view, side, legal)]


class Nash(Forecaster):
    """Rule 8: neither guesses the foe nor fears its worst reply alone. Both sides choose at once:
    the odds after every pair (option, reply) make a zero-sum game, and the bot draws its option
    from that game's optimal mixed strategy, so that no foe can read it. One turn deep, like
    rules 6 and 7; the replies are those of rule 7.

    The draw uses the bot's own seed: the same seed plays the same battle.
    """

    def __init__(self, model: WinModel | None = None, *, seed: int = 0) -> None:
        super().__init__(model)
        self._rng = random.Random(seed)

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        strategy = self._search.nash_strategy(view, side, legal)
        return self._rng.choices(legal, weights=strategy)[0]


@cache
def _shipped_model() -> WinModel:
    return WinModel.load(SHIPPED)
