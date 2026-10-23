from collections.abc import Iterator

from trainerlab._engine import Action
from trainerlab.environment import BattleEnvironment
from trainerlab.human import HumanPlayer
from trainerlab.render import Narration, Style
from trainerlab.screen import Screen

TEAM = ["Gyarados", "Snorlax", "Toxapex"]


class Console:
    """Scripted keyboard answers, and everything the screen shows, as plain lines."""

    def __init__(self, answers: list[str]) -> None:
        self._answers: Iterator[str] = iter(answers)
        self.shown: list[str] = []
        self.prompts = 0
        self.screen = Screen(lambda text: None, width=80, speed="instant", log=self.shown.append)

    def ask(self, prompt: str) -> str:
        self.prompts += 1
        return next(self._answers)

    def player(self, hints: bool = False) -> HumanPlayer:
        return HumanPlayer(self.ask, self.screen, Style(enabled=False), hints=hints)

    def act(self, team: list[str], foe: list[str], hints: bool = False) -> Action:
        env = BattleEnvironment(team, foe, seed=1)
        return self.player(hints).choose_action(0, env.observe(0), env.legal_actions(0))


def test_the_menu_lists_the_moves_then_one_entry_to_switch() -> None:
    console = Console(["2"])

    action = console.act(TEAM, ["Blissey"])

    assert action == Action.move(1)
    assert "== Turn 1 ==" in console.shown
    menu = console.shown[console.shown.index("== What will you do? ==") + 1 :][:6]
    assert menu == [
        "1. Dragon Dance (Dragon, status, 20/20 PP)",
        "2. Waterfall (Water, 80 power, 15/15 PP)",
        "3. Earthquake (Ground, 100 power, 10/10 PP)",
        "4. Ice Fang (Ice, 65 power, 15/15 PP)",
        "5. Switch…",
        "?. Details of your moves",
    ]
    assert "> Waterfall (Water, 80 power, 15/15 PP)" in console.shown


def test_switching_opens_a_second_menu_of_who_can_come_in() -> None:
    console = Console(["5", "2"])

    action = console.act(TEAM, ["Blissey"])

    assert action == Action.switch(2)
    submenu = console.shown[console.shown.index("== Switch to… ==") + 1 :][:3]
    assert submenu == [
        "1. Snorlax (Normal, 524/524 HP)",
        "2. Toxapex (Poison/Water, 304/304 HP)",
        "0. Back",
    ]


def test_back_returns_to_the_moves() -> None:
    console = Console(["5", "0", "3"])

    assert console.act(TEAM, ["Blissey"]) == Action.move(2)
    assert console.shown.count("== What will you do? ==") == 2


def test_a_wrong_answer_in_the_switch_menu_is_asked_again() -> None:
    console = Console(["5", "9", "x", "1"])

    assert console.act(TEAM, ["Blissey"]) == Action.switch(1)
    assert console.shown.count("Please type a number between 0 and 2.") == 2


def test_a_lone_pokemon_has_no_switch_entry() -> None:
    console = Console(["5", "1"])

    console.act(["Gyarados"], ["Blissey"])

    assert "5. Switch…" not in console.shown
    assert "Please type a number between 1 and 4." in console.shown


def test_hints_reach_both_menus() -> None:
    console = Console(["5", "1"])

    console.act(["Gyarados", "Snorlax"], ["Infernape"], hints=True)

    assert "2. Waterfall (Water, 80 power, 15/15 PP)  x2" in console.shown
    assert "1. Snorlax (Normal, 524/524 HP)  weak to Fighting" in console.shown


def test_a_wrong_answer_is_asked_again() -> None:
    console = Console(["", "zero", "0", "9", " 3 "])

    action = console.act(["Gyarados"], ["Blissey"])

    assert action == Action.move(2)
    assert console.prompts == 5
    assert console.shown.count("Please type a number between 1 and 4.") == 4


def test_a_question_mark_shows_the_move_details_then_asks_again() -> None:
    console = Console(["?", "1"])

    console.act(["Gyarados"], ["Blissey"])

    assert "== Your moves ==" in console.shown
    assert "Waterfall: Water, physical, 80 power, 100% accuracy, 15/15 PP" in console.shown


def test_the_last_turn_is_shown_above_the_next_choice() -> None:
    console = Console(["1"])
    console.screen.narrate("Turn 1", [Narration("Something happened.", False, 0, 0, 0, 0)])
    console.shown.clear()

    console.act(["Gyarados"], ["Blissey"])

    assert console.shown[console.shown.index("== Last turn ==") + 1] == "Something happened."


def test_the_player_picks_a_replacement_among_the_options() -> None:
    env = BattleEnvironment(TEAM, ["Blissey"], seed=1)
    console = Console(["?", "0", "2"])

    index = console.player().choose_replacement(0, env.observe(0), [1, 2])

    assert index == 2
    assert "== Who comes in? ==" in console.shown
    assert "2. Toxapex (Poison/Water, 304/304 HP)" in console.shown
    assert "0. Back" not in console.shown  # a replacement cannot be undone
    assert "== Your moves ==" not in console.shown  # nor does it show move details
