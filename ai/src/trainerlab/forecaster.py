"""Rule 6: look one turn ahead, and play what the win probability model rates best.

For each of its options, the bot plays the turn on a copy of what it sees, the foe playing what
rule 5 would play, then asks the model how likely it is to win from the position reached. A
replacement is chosen the same way, without a turn. The model ships with the package
(models/win-logistic.npz): the logistic regression measured in the README, rebuilt by

    trainerlab dataset calculator survivor --pairs 2000 --from 40000
    trainerlab train calculator-vs-survivor-40000-41999.npz --out models/win-logistic.npz
"""

import copy
from functools import cache
from pathlib import Path

import numpy as np

from trainerlab import _engine
from trainerlab.bots import OutlookBot
from trainerlab.features import feature_names, features
from trainerlab.model import WinModel

MODEL = Path(__file__).parent / "models" / "win-logistic.npz"
SAMPLES = 2  # turns played per option, on the same seeds for every option


class ForecastBot:
    def __init__(self, model: WinModel | None = None) -> None:
        self._model = model if model is not None else _shipped_model()
        if self._model.feature_names != feature_names():
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
            return self._value(after, side)

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
            total += self._value(after, side)
        return total / SAMPLES

    def _value(self, state: _engine.BattleState, side: int) -> float:
        """The probability of winning from this position, as the model sees it."""
        mine, theirs = _engine.side_has_lost(state, side), _engine.side_has_lost(state, 1 - side)
        if mine or theirs:
            return 0.5 if mine and theirs else 0.0 if mine else 1.0
        x = features(_engine.observe(state, side), side)
        return float(self._model.predict(x[np.newaxis])[0])


@cache
def _shipped_model() -> WinModel:
    return WinModel.load(MODEL)


def playable(view: _engine.BattleState, foe: int) -> _engine.BattleState:
    """A state the engine accepts, built from a view: each foe Pokemon not yet seen becomes a copy
    of the foe's active, still unseen. It only fills the slot: observe hides it again."""
    state = copy.copy(view)
    stand_in = _engine.make_combatant(view.active(foe).species_id)
    for index in range(view.team_size[foe]):
        if view.pokemon(foe, index).is_empty():
            state.set_pokemon(foe, index, stand_in)
    return state


def _foe_action(state: _engine.BattleState, foe: int) -> _engine.Action:
    """What rule 5 would play in the foe's place."""
    legal = _engine.legal_actions(state, foe)
    return OutlookBot().choose_action(foe, _engine.observe(state, foe), legal)
