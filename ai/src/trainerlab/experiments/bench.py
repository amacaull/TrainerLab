"""How fast the bots play from Python: the reference the C++ search is measured against.

Every line repeats one thing for a few seconds, on positions like those of `search_bench` in C++:
random battles, played a few random turns on. The teams differ, so only the orders of magnitude
compare.
"""

import time
from collections.abc import Callable
from dataclasses import dataclass
from itertools import count, cycle

from trainerlab import _engine
from trainerlab.bots.baseline import RandomBot
from trainerlab.bots.lookahead import Forecaster
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment, random_team_pair

POSITIONS = 16
OPENING_TURNS = 3


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


def positions(how_many: int = POSITIONS) -> list[BattleEnvironment]:
    """Random battles, a few random turns in, with both actives standing."""
    found = []
    for seed in count():
        env = BattleEnvironment(*random_team_pair(seed), seed=seed, max_turns=OPENING_TURNS)
        play_battle(env, RandomBot(seed=2 * seed), RandomBot(seed=2 * seed + 1))
        if env.legal_actions(0) and env.legal_actions(1):  # neither active fainted
            found.append(env)
        if len(found) == how_many:
            return found
    raise AssertionError("unreachable")  # pragma: no cover


def measure(seconds: float) -> list[Line]:
    """Each line, measured for `seconds`."""
    envs = cycle(positions())
    seeds = count()

    def playout() -> int:
        seed = next(seeds)
        env = BattleEnvironment(*random_team_pair(seed), seed=seed)
        play_battle(env, RandomBot(seed=2 * seed), RandomBot(seed=2 * seed + 1))
        return env.turn

    forecaster = Forecaster()

    def decision() -> int:
        env = next(envs)
        forecaster.choose_action(0, env.observe(0), env.legal_actions(0))
        return 1

    def damage() -> int:
        _engine.expected_damage(next(envs).observe(0), 0, 0)
        return 1

    return [
        Line("random playout, Python loop", "turns", rate(playout, seconds)),
        Line("forecaster decision (rule 6)", "decisions", rate(decision, seconds)),
        Line("expected damage, first move", "calls", rate(damage, seconds)),
    ]
