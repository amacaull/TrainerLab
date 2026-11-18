"""How fast the bots play from Python: the reference the C++ search is measured against.

Every line repeats one thing for a few seconds, on the 64 positions of `search_bench` in C++
(`_search.openings`): random battles, 3 random turns in, seen by side 0.
"""

import time
from collections.abc import Callable
from dataclasses import dataclass
from itertools import count, cycle

import numpy as np

from trainerlab import _engine, _search
from trainerlab.bots.baseline import RandomBot
from trainerlab.bots.lookahead import Forecaster, Mcts, Nash
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import DATA_DIR, BattleEnvironment, random_team_pair
from trainerlab.learning.env import VecEnv, random_actions
from trainerlab.learning.features import features

POSITIONS = 64  # search_bench plays the same ones


@dataclass(frozen=True)
class Line:
    what: str
    unit: str
    per_second: float


def rate(
    step: Callable[[], int], seconds: float, clock: Callable[[], float] = time.perf_counter
) -> float:
    """Calls `step`, which returns how many units it did, until `seconds` have passed: units per
    second."""
    units, start = 0, clock()
    while True:
        units += step()
        elapsed = clock() - start
        if elapsed >= seconds:
            return units / elapsed


def positions(how_many: int = POSITIONS) -> list[_engine.BattleState]:
    """The positions of the benchmark: the same as in C++, built by the same function."""
    _engine.engine_init(str(DATA_DIR))  # each binding holds its own copy of the engine
    _search.init(str(DATA_DIR))
    return _search.openings(how_many)


def measure(seconds: float) -> list[Line]:
    """Each line, measured for `seconds`."""
    states = positions()
    views = [_engine.observe(state, 0) for state in states]
    next_state, next_view = cycle(states), cycle(views)
    seeds = count()

    def playout() -> int:
        seed = next(seeds)
        env = BattleEnvironment(*random_team_pair(seed), seed=seed)
        play_battle(env, RandomBot(seed=2 * seed), RandomBot(seed=2 * seed + 1))
        return env.turn

    forecaster = Forecaster()

    def decision() -> int:
        state = next(next_state)
        forecaster.choose_action(0, _engine.observe(state, 0), _engine.legal_actions(state, 0))
        return 1

    nash = Nash()

    def nash_decision() -> int:
        state = next(next_state)
        nash.choose_action(0, _engine.observe(state, 0), _engine.legal_actions(state, 0))
        return 1

    mcts = Mcts()

    def mcts_decision() -> int:
        state = next(next_state)
        mcts.choose_action(0, _engine.observe(state, 0), _engine.legal_actions(state, 0))
        return 1

    env, rng = VecEnv(256, seed=1), np.random.default_rng(0)

    def env_step() -> int:
        env.step(random_actions(env.observe().mask, rng))
        return env.battles

    def damage() -> int:
        _engine.expected_damage(next(next_view), 0, 0)
        return 1

    def python_features() -> int:
        features(next(next_view), 0)
        return 1

    def cpp_features() -> int:
        _search.features(next(next_view), 0)
        return 1

    return [
        Line("random playout, Python loop", "turns", rate(playout, seconds)),
        Line("forecaster decision (rule 6)", "decisions", rate(decision, seconds)),
        Line("nash decision (rule 8)", "decisions", rate(nash_decision, seconds)),
        Line("mcts decision (rule 9)", "decisions", rate(mcts_decision, seconds)),
        Line("vector env, 256 battles: observe + step", "steps", rate(env_step, seconds)),
        Line("expected damage, first move", "calls", rate(damage, seconds)),
        Line("features of a position, Python", "positions", rate(python_features, seconds)),
        Line("features of a position, C++", "positions", rate(cpp_features, seconds)),
    ]
