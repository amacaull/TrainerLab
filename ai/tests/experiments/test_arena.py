from pathlib import Path

import numpy as np
import pytest

from trainerlab import _engine, _search
from trainerlab.bots.roster import ALPHAZERO, RULE14, Searcher, searcher
from trainerlab.experiments import arena
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.learning.encoding import ACTIONS
from trainerlab.rl.planner import REPLACEMENT_SHARE, cpp_network


def test_the_search_bots_are_known_with_their_network_depth_and_replacements() -> None:
    assert searcher("alphazero") == Searcher(ALPHAZERO, 200, True)
    assert searcher("focus@50") == Searcher(RULE14, 50, False)
    assert searcher(f"plan:{ALPHAZERO}@8") == Searcher(ALPHAZERO.resolve(), 8, True)
    assert searcher("hitter") is None and searcher("net:x.pt") is None


class _Seeded:
    """Rule 11 as a measure plays it in C++: the searches of a battle seeded seed * 1'000'003 +
    0, 1, 2… in the order they are made, both sides counting together."""

    def __init__(
        self, network: _search.Network, seed: int, made: list[int], simulations: int
    ) -> None:
        self.network, self.seed, self.made, self.simulations = network, seed, made, simulations

    def _next(self) -> int:
        self.made[0] += 1
        return self.seed * 1_000_003 + self.made[0] - 1

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        if len(legal) == 1:
            return legal[0]
        index = self.network.plan_action(view, side, legal, self._next(), self.simulations)
        return legal[index]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        if len(options) > 1:
            share = max(2, self.simulations // REPLACEMENT_SHARE)
            return self.network.plan_replacement(view, side, options, self._next(), share)
        mask = np.zeros(ACTIONS, dtype=np.float32)
        mask[[4 + index for index in options]] = 1
        logits, _ = self.network.evaluate(view, side, mask)
        return int(np.argmax(logits)) - 4


def test_a_measure_s_battles_play_as_the_python_referee_plays_them() -> None:
    bot = Searcher(ALPHAZERO, 8, True)
    battles = [(*random_team_pair(seed, 3), 50 + seed) for seed in range(6)]

    winners = arena.play(bot, battles, jobs=1)

    network = cpp_network(ALPHAZERO)
    for (team0, team1, seed), winner in zip(battles, winners, strict=True):
        made = [0]
        bot0, bot1 = (_Seeded(network, seed, made, 8) for _ in (0, 1))
        expected = play_battle(BattleEnvironment(team0, team1, seed=seed), bot0, bot1)
        assert winner == expected


def test_the_winners_come_in_order_and_the_progress_is_told_from_the_start() -> None:
    battles = [(*random_team_pair(seed, 2), seed) for seed in range(7)]
    told: list[int] = []

    one = arena.play(Searcher(ALPHAZERO, 4, False), battles, jobs=2, done=told.append)
    again = arena.play(Searcher(ALPHAZERO, 4, False), battles, jobs=2)

    assert one == again and len(one) == 7
    assert set(one) <= {0, 1, None}
    assert told[0] == 0 and told[-1] == 7 and told == sorted(told)


def test_no_battle_no_call() -> None:
    assert arena.play(Searcher(Path("missing.pt"), 4, False), [], jobs=1) == []


def test_a_variable_keeps_the_battles_in_python_one_by_one(monkeypatch: pytest.MonkeyPatch) -> None:
    assert arena.searcher_of("alphazero") == searcher("alphazero")
    monkeypatch.setenv("TRAINERLAB_ONE_BY_ONE", "1")
    assert arena.searcher_of("alphazero") is None


def test_ctrl_c_stops_the_battles_at_once() -> None:
    import os
    import signal
    import threading
    import time

    battles = [(*random_team_pair(seed, 6), seed) for seed in range(400)]
    threading.Timer(0.5, os.kill, (os.getpid(), signal.SIGINT)).start()
    start = time.monotonic()

    with pytest.raises(KeyboardInterrupt):
        arena.play(Searcher(ALPHAZERO, 50, True), battles, jobs=2)

    assert time.monotonic() - start < 10
