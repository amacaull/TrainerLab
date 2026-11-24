"""
TrainerLab search: the C++ side of the AI.
"""
from __future__ import annotations
import collections.abc
import numpy
import numpy.typing
import trainerlab._engine
import typing
__all__: list[str] = ['Forecaster', 'Network', 'VecEnv', 'action_mask', 'encode', 'encoding_names', 'feature_names', 'features', 'init', 'openings']
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
class VecEnv:
    """
    Many battles at once, both sides played by the caller; a battle that ends starts again.
    """
    def __init__(self, count: typing.SupportsInt | typing.SupportsIndex, seed: typing.SupportsInt | typing.SupportsIndex, max_turns: typing.SupportsInt | typing.SupportsIndex = 1000) -> None:
        ...
    def observe(self) -> tuple[numpy.typing.NDArray[numpy.float32], numpy.typing.NDArray[numpy.int32], numpy.typing.NDArray[numpy.float32], numpy.typing.NDArray[numpy.float32]]:
        """
        (pokemon[N, 2, 12, P], ids[N, 2, 12, 7], field[N, 2, F], mask[N, 2, 10]), per battle and side.
        """
    def step(self, actions: typing.Annotated[numpy.typing.ArrayLike, numpy.int32]) -> tuple[numpy.typing.NDArray[numpy.float32], typing.Any]:
        """
        Plays actions[N, 2]; (rewards[N, 2], done[N]). A side with no decision is ignored.
        """
    @property
    def count(self) -> int:
        ...
    @property
    def finished(self) -> int:
        """
        Battles finished since the start.
        """
class Network:
    """
    The policy and value network of trainerlab.rl.net, evaluated in C++.
    """
    def __init__(self, weights: collections.abc.Mapping[str, typing.Annotated[numpy.typing.ArrayLike, numpy.float32]]) -> None:
        """
        The weights by their PyTorch names (state_dict).
        """
    def evaluate(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, mask: typing.Annotated[numpy.typing.ArrayLike, numpy.float32]) -> tuple[numpy.typing.NDArray[numpy.float32], float]:
        """
        (logits[10], value) for what `side` sees in `view`, its own observe().
        """
    def judge(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, legal: collections.abc.Sequence[trainerlab._engine.Action], seed: typing.SupportsInt | typing.SupportsIndex, simulations: typing.SupportsInt | typing.SupportsIndex, worlds: typing.SupportsInt | typing.SupportsIndex, min_visits: typing.SupportsInt | typing.SupportsIndex) -> tuple[numpy.typing.NDArray[numpy.int32], numpy.typing.NDArray[numpy.float64]]:
        """
        A judge's look at a decision: (visits[10], value[10]) per option of `side` at the root of a search that tries every legal option at least `min_visits` times; the value is the mean result for `side`, in [-1, 1].
        """
    def plan_action(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, legal: collections.abc.Sequence[trainerlab._engine.Action], seed: typing.SupportsInt | typing.SupportsIndex, simulations: typing.SupportsInt | typing.SupportsIndex = 200) -> int:
        """
        Rule 11: the index, in `legal`, of the option a tree search guided by this network visits most, after `simulations` simulations; the same seed makes the same decision.
        """
    def play_matches(self, starts: collections.abc.Sequence[trainerlab._engine.BattleState], seeds: collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], threads: typing.SupportsInt | typing.SupportsIndex, simulations: typing.SupportsInt | typing.SupportsIndex = 200, replacement_simulations: typing.SupportsInt | typing.SupportsIndex = 50, lanes: typing.SupportsInt | typing.SupportsIndex = 16, max_turns: typing.SupportsInt | typing.SupportsIndex = 1000) -> tuple:
        """
        Palier 12.3: the battles of a measure, rule 11 with this network on both sides, side by side (search/include/search/self_play.hpp): battle k from starts[k] (started), its dice and searches from seeds[k], as Python's referee plays it. (winner[k]: 0, 1 or -1 for a draw, turns[k]).
        """
    def plan_replacement(self, view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex, options: collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], seed: typing.SupportsInt | typing.SupportsIndex, simulations: typing.SupportsInt | typing.SupportsIndex) -> int:
        """
        Palier 10.4: the team index, among `options`, whose replacement leaves `side` the best value, each searched `simulations` simulations; the same seed makes the same choice.
        """
    def self_play(self, games: typing.SupportsInt | typing.SupportsIndex, seed: typing.SupportsInt | typing.SupportsIndex, threads: typing.SupportsInt | typing.SupportsIndex, simulations: typing.SupportsInt | typing.SupportsIndex = 200, worlds: typing.SupportsInt | typing.SupportsIndex = 8, noise: typing.SupportsFloat | typing.SupportsIndex = 0.25, max_turns: typing.SupportsInt | typing.SupportsIndex = 1000, lanes: typing.SupportsInt | typing.SupportsIndex = 16, fast: typing.SupportsInt | typing.SupportsIndex = 0, full_share: typing.SupportsFloat | typing.SupportsIndex = 1.0, replacement_simulations: typing.SupportsInt | typing.SupportsIndex = 0, team_sizes: collections.abc.Sequence[tuple[typing.SupportsInt | typing.SupportsIndex, typing.SupportsFloat | typing.SupportsIndex]] = [(6, 1.0)]) -> dict:
        """
        The AlphaZero loop's games (search/include/search/self_play.hpp): rule 11 against itself, with noise at the root; per decision the encoding, the mask, the share of visits per option (policy), the result for that side, the search's value for it (search_value) and what the foe played (foe, foe_mask); per battle its turns and winner (-1 for a draw). Each thread plays `lanes` battles side by side, their network queries batched. With `fast`, only a share `full_share` of the searches are full and recorded; the others search `fast` simulations, unrecorded. With `replacement_simulations`, each replacement with a choice is searched and recorded (foe -1). Teams are cut to `team_sizes` (size, share) pairs, drawn per battle (team_size).
        """
def action_mask(legal: collections.abc.Sequence[trainerlab._engine.Action]) -> numpy.typing.NDArray[numpy.float32]:
    """
    1 for each legal option: moves 0-3 by slot, then switches 4-9.
    """
def encode(view: trainerlab._engine.BattleState, side: typing.SupportsInt | typing.SupportsIndex) -> tuple[numpy.typing.NDArray[numpy.float32], numpy.typing.NDArray[numpy.int32], numpy.typing.NDArray[numpy.float32]]:
    """
    What `side` sees, for a network: (pokemon[12, P], ids[12, 7], field[F]).
    """
def encoding_names() -> tuple[list[str], list[str]]:
    """
    The names of the columns of a Pokemon's vector, and of the field's.
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
