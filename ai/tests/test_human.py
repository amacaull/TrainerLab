from collections.abc import Iterator

from trainerlab._engine import Action
from trainerlab.environment import BattleEnvironment
from trainerlab.human import HumanPlayer
from trainerlab.render import Style


class Console:
    """Scripted keyboard answers, and everything shown to the player."""

    def __init__(self, answers: list[str]) -> None:
        self._answers: Iterator[str] = iter(answers)
        self.shown: list[str] = []
        self.prompts = 0

    def ask(self, prompt: str) -> str:
        self.prompts += 1
        return next(self._answers)

    def say(self, line: str) -> None:
        self.shown.append(line)


def _player(console: Console) -> HumanPlayer:
    return HumanPlayer(console.ask, console.say, Style(enabled=False))


def test_the_player_sees_the_board_and_numbered_options_then_picks_one() -> None:
    env = BattleEnvironment(["Gyarados", "Snorlax"], ["Blissey"], seed=1)
    console = Console(["2"])

    action = _player(console).choose_action(0, env.observe(0), env.legal_actions(0))

    assert action == env.legal_actions(0)[1]
    assert "--- Turn 1 ---" in console.shown
    assert "What will you do?" in console.shown
    assert "  2. Waterfall (Water, 80 power, 15/15 PP)" in console.shown
    assert "  5. Switch to Snorlax (524/524 HP)" in console.shown


def test_a_wrong_answer_is_asked_again() -> None:
    env = BattleEnvironment(["Gyarados"], ["Blissey"], seed=1)
    console = Console(["", "zero", "0", "9", " 3 "])

    action = _player(console).choose_action(0, env.observe(0), env.legal_actions(0))

    assert action == Action.move(2)
    assert console.prompts == 5
    assert console.shown.count("Please type a number between 1 and 4.") == 4


def test_the_player_picks_a_replacement_among_the_options() -> None:
    env = BattleEnvironment(["Gyarados", "Snorlax", "Toxapex"], ["Blissey"], seed=1)
    console = Console(["2"])

    index = _player(console).choose_replacement(0, env.observe(0), [1, 2])

    assert index == 2
    assert console.shown == [
        "Who comes in?",
        "  1. Switch to Snorlax (524/524 HP)",
        "  2. Switch to Toxapex (304/304 HP)",
    ]
