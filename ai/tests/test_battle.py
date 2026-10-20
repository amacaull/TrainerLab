import pytest

from trainerlab import _engine
from trainerlab._engine import Action, ActionError
from trainerlab.battle import play_battle
from trainerlab.bots import RandomBot
from trainerlab.environment import BattleEnvironment

TEAM0 = ["Dragapult", "Snorlax", "Toxapex", "Excadrill", "Blissey", "Corviknight"]
TEAM1 = ["Gyarados", "KommoO", "Infernape", "Primarina", "TapuKoko", "Mimikyu"]


class SpyBot:
    """Plays like a random bot and records every side number and view it receives."""

    def __init__(self) -> None:
        self._inner = RandomBot(seed=0)
        self.sides: list[int] = []
        self.views: list[_engine.BattleState] = []

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        self.sides.append(side)
        self.views.append(view)
        return self._inner.choose_action(side, view, legal)

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        self.sides.append(side)
        self.views.append(view)
        return self._inner.choose_replacement(side, view, options)


class CheatingBot:
    """Always asks to switch to the Pokemon already in play: never a legal action."""

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        return Action.switch(view.active_index[side])

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        return options[0]


@pytest.mark.parametrize("seed", range(20))
def test_a_battle_between_two_bots_ends_with_a_result(seed: int) -> None:
    env = BattleEnvironment(TEAM0, TEAM1, seed=seed)

    winner = play_battle(env, RandomBot(seed=seed), RandomBot(seed=seed + 1))

    assert env.is_over()
    assert winner in (0, 1, None)
    assert winner == env.winner()


def test_each_bot_sees_only_its_own_side_view() -> None:
    env = BattleEnvironment(TEAM0, TEAM1, seed=42)
    spy = SpyBot()

    play_battle(env, RandomBot(seed=1), spy)

    assert spy.sides
    assert set(spy.sides) == {1}
    first_view = spy.views[0]
    assert not first_view.pokemon(0, 0).is_empty()
    assert all(first_view.pokemon(0, i).is_empty() for i in range(1, 6))


def test_an_illegal_choice_stops_the_battle_with_an_error() -> None:
    env = BattleEnvironment(TEAM0, TEAM1, seed=42)

    with pytest.raises(ActionError):
        play_battle(env, CheatingBot(), RandomBot(seed=1))


def test_the_same_seeds_replay_the_same_battle() -> None:
    first_env = BattleEnvironment(TEAM0, TEAM1, seed=42)
    second_env = BattleEnvironment(TEAM0, TEAM1, seed=42)

    first_winner = play_battle(first_env, RandomBot(seed=1), RandomBot(seed=2))
    second_winner = play_battle(second_env, RandomBot(seed=1), RandomBot(seed=2))

    assert first_winner == second_winner
    assert first_env.turn == second_env.turn
    assert first_env.observe(0) == second_env.observe(0)


class ScriptedBot:
    """Plays a fixed move every turn and records the side of each replacement it is asked for."""

    def __init__(self, move_slot: int, replacements: list[int]) -> None:
        self._move = Action.move(move_slot)
        self._replacements = replacements

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        return self._move if self._move in legal else legal[0]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        self._replacements.append(side)
        return options[0]


def test_after_a_double_knock_out_the_loop_asks_the_faster_side_first() -> None:
    env = BattleEnvironment(["MegaBanette", "Snorlax"], ["Excadrill", "Blissey"], seed=42)
    replacements: list[int] = []
    destiny_bond = ScriptedBot(move_slot=3, replacements=replacements)
    earthquake = ScriptedBot(move_slot=0, replacements=replacements)

    play_battle(env, destiny_bond, earthquake)

    assert replacements[:2] == [1, 0]
