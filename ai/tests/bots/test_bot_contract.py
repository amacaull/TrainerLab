"""The contract every bot must honour, replayed on each of them (Liskov substitution)."""

from collections.abc import Callable

import pytest
from teams import TEAM0, TEAM1

from trainerlab import _engine
from trainerlab.bots.baseline import RandomBot
from trainerlab.bots.lookahead import Forecaster, Strategist
from trainerlab.bots.rules import calculator, dodger, hitter, matcher, survivor
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.player import Bot

# Every new bot is added here and inherits the whole contract.
BOT_FACTORIES: list[Callable[[int], Bot]] = [
    lambda seed: RandomBot(seed=seed),
    lambda seed: hitter(),
    lambda seed: dodger(),
    lambda seed: matcher(),
    lambda seed: survivor(),
    lambda seed: calculator(),
    lambda seed: Forecaster(),
    lambda seed: Strategist(),
]


class ContractChecker:
    """Wraps a bot and fails the test as soon as it returns a choice outside its list."""

    def __init__(self, bot: Bot) -> None:
        self._bot = bot
        self.decisions = 0

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        action = self._bot.choose_action(side, view, legal)
        assert action in legal, f"{action!r} is not among the legal actions {legal!r}"
        self.decisions += 1
        return action

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        team_index = self._bot.choose_replacement(side, view, options)
        assert team_index in options, f"{team_index} is not among the options {options}"
        self.decisions += 1
        return team_index


@pytest.mark.parametrize("make_bot", BOT_FACTORIES)
@pytest.mark.parametrize("side", [0, 1])
@pytest.mark.parametrize("seed", range(5))
def test_a_bot_only_ever_returns_a_listed_choice(
    make_bot: Callable[[int], Bot], side: int, seed: int
) -> None:
    env = BattleEnvironment(TEAM0, TEAM1, seed=seed)
    checked = ContractChecker(make_bot(seed))
    opponent = RandomBot(seed=seed + 100)
    bots = (checked, opponent) if side == 0 else (opponent, checked)

    play_battle(env, *bots)

    assert checked.decisions > 0
