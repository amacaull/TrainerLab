"""Rules 6 and 7: look one turn ahead, and play what the win probability model rates best.

For each of its options, the bot plays the turn on a copy of what it sees, the foe playing what
rule 5 would play, then asks the model how likely it is to win from the position reached. A
replacement is chosen the same way, without a turn. The model ships with the package
(learning/models/win-logistic.npz): the logistic regression measured in the README, rebuilt by

    trainerlab dataset calculator survivor --pairs 2000 --from 40000
    trainerlab train calculator-vs-survivor-40000-41999.npz \
      --out ai/src/trainerlab/learning/models/win-logistic.npz
"""

import copy
from functools import cache

import numpy as np

from trainerlab import _engine
from trainerlab.bots.rules import calculator
from trainerlab.game.views import playable
from trainerlab.learning.features import feature_names, features
from trainerlab.learning.model import SHIPPED, WinModel

SAMPLES = 2  # turns played per option, on the same seeds for every option


class Forecaster:
    """Rule 6: plays each option a turn ahead, the foe playing as rule 5, and keeps the option
    whose position the model rates best."""

    def __init__(self, model: WinModel | None = None) -> None:
        self.model = model if model is not None else _shipped_model()
        if self.model.feature_names != feature_names():
            raise ValueError("the model was trained on other features: train it again")

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        state = playable(view, 1 - side)
        foe_action = _foe_action(state, 1 - side)
        return max(legal, key=lambda action: self._after_turn(state, side, action, foe_action))

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        state = playable(view, 1 - side)

        def value(index: int) -> float:
            after = copy.copy(state)
            _engine.resolve_replacement(after, side, index)
            return self.odds(after, side)

        return max(options, key=value)

    def _after_turn(
        self,
        state: _engine.BattleState,
        side: int,
        action: _engine.Action,
        foe_action: _engine.Action,
    ) -> float:
        actions = (action, foe_action) if side == 0 else (foe_action, action)
        total = 0.0
        for sample in range(SAMPLES):
            after = copy.copy(state)
            _engine.resolve_turn(after, *actions, state.turn * SAMPLES + sample)
            total += self.odds(after, side)
        return total / SAMPLES

    def odds(self, state: _engine.BattleState, side: int) -> float:
        """The probability of winning from this position, as the model sees it."""
        lost, won = _engine.side_has_lost(state, side), _engine.side_has_lost(state, 1 - side)
        if lost or won:
            return 0.5 if lost and won else float(won)  # the battle is over: no need to guess
        x = features(_engine.observe(state, side), side)
        return float(self.model.predict(x[np.newaxis])[0])


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
        state = playable(view, 1 - side)
        foe_replies = replies(view, state, 1 - side)
        best, best_value = legal[0], -1.0
        for action in legal:
            worst = 2.0
            for reply in foe_replies:
                worst = min(worst, self._after_turn(state, side, action, reply))
                if worst <= best_value:
                    break  # already no better than the best option: its worst case is lower
            if worst > best_value:
                best, best_value = action, worst
        return best


def replies(
    view: _engine.BattleState, state: _engine.BattleState, foe: int
) -> list[_engine.Action]:
    """What the foe may do, but switch to a Pokemon the bot has not seen."""
    return [
        action
        for action in _engine.legal_actions(state, foe)
        if action.kind == _engine.ACTION_MOVE or not view.pokemon(foe, action.index).is_empty()
    ]


@cache
def _shipped_model() -> WinModel:
    return WinModel.load(SHIPPED)


def _foe_action(state: _engine.BattleState, foe: int) -> _engine.Action:
    """What rule 5 would play in the foe's place."""
    legal = _engine.legal_actions(state, foe)
    return calculator().choose_action(foe, _engine.observe(state, foe), legal)
