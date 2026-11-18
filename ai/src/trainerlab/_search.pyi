"""
TrainerLab search: the C++ side of the AI.
"""
from __future__ import annotations
import numpy
import numpy.typing
import trainerlab._engine
import typing
__all__: list[str] = ['feature_names', 'features', 'init', 'openings']
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
