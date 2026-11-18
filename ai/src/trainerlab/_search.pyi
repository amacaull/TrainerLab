"""
TrainerLab search: the C++ side of the AI.
"""
from __future__ import annotations
import collections.abc
import numpy
import numpy.typing
import trainerlab._engine
import typing
__all__: list[str] = ['Forecaster', 'feature_names', 'features', 'init', 'openings']
class Forecaster:
    """
    Rules 6 to 9 in C++, with the layers of a WinModel.
    """
    def __init__(self, weights: collections.abc.Sequence[typing.Annotated[numpy.typing.ArrayLike, numpy.float64]], biases: collections.abc.Sequence[typing.Annotated[numpy.typing.ArrayLike, numpy.float64]], mean: typing.Annotated[numpy.typing.ArrayLike, numpy.float64], std: typing.Annotated[numpy.typing.ArrayLike, numpy.float64]) -> None:
        ...
    def calculator_action(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, legal: collections.abc.Sequence[trainerlab._engine.Action]) -> trainerlab._engine.Action:
        """
        What rule 5 plays from this view: the foe the forecaster assumes.
        """
    def choose_action(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, legal: collections.abc.Sequence[trainerlab._engine.Action]) -> int:
        """
        The index, in `legal`, of the forecaster's choice (rule 6).
        """
    def choose_replacement(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, options: collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex]) -> int:
        """
        The index, in `options`, of the Pokemon sent in.
        """
    def choose_strategist_action(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, legal: collections.abc.Sequence[trainerlab._engine.Action]) -> int:
        """
        The index, in `legal`, of the strategist's choice (rule 7).
        """
    def mcts_action(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, legal: collections.abc.Sequence[trainerlab._engine.Action], seed: typing.SupportsInt | typing.SupportsIndex) -> int:
        """
        Rule 9: the index, in `legal`, of the option a Monte Carlo tree search over simultaneous moves visits most; the same seed makes the same decision.
        """
    def nash_strategy(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, legal: collections.abc.Sequence[trainerlab._engine.Action]) -> list[float]:
        """
        Rule 8: one probability per option in `legal`, the optimal mixed strategy of the simultaneous game the odds define.
        """
    def odds(self, state: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex) -> float:
        """
        The probability of winning from this position, as the model sees it.
        """
def feature_names() -> list[str]:
    """
    The name of each column of features(), in order.
    """
def features(view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex) -> numpy.typing.NDArray[numpy.float32]:
    """
    What `side` saw in `view`, its own observe(), as float32: the same values as trainerlab.learning.features.features.
    """
def init(data_dir: str) -> None:
    """
    Loads the catalog once; again with the same path does nothing.
    """
def openings(count: typing.SupportsInt | typing.SupportsIndex, turns: typing.SupportsInt | typing.SupportsIndex = 3) -> list[trainerlab._engine.BattleState]:
    """
    Random battles from seeds 0, 1, 2..., played `turns` random turns on, with both actives standing: the positions of the benchmark.
    """
