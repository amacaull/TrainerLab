import pytest
from prompter import Scripted

from trainerlab._engine import Action
from trainerlab.game.environment import BattleEnvironment
from trainerlab.terminal.human import BACK, FLEE, SWITCH, Fled, HumanPlayer
from trainerlab.terminal.prompts import Option, Separator, plain

TEAM = ["Gyarados", "Snorlax", "Toxapex"]


def _act(prompter: Scripted, drawn: list[int] | None = None) -> Action:
    env = BattleEnvironment(TEAM, ["Blissey"], seed=1)
    view = env.observe(0)
    seen = drawn if drawn is not None else []
    player = HumanPlayer(prompter, lambda v: seen.append(v.turn))
    return player.choose_action(0, view, env.legal_actions(0))


def test_the_battle_menu_lists_the_moves_then_one_entry_to_switch() -> None:
    waterfall = Action.move(1)
    prompter = Scripted([waterfall])
    drawn: list[int] = []

    assert _act(prompter, drawn) == waterfall

    assert drawn == [0]
    kind, message, options = prompter.asked[0]
    assert message == "What will Gyarados do?"
    labels = [plain(o.label).split()[0] for o in options if isinstance(o, Option)]
    assert labels == ["Dragon", "Waterfall", "Earthquake", "Ice", "Switch", "Run"]
    assert isinstance(options[4], Separator)
    switch, flee = options[-2:]
    assert isinstance(switch, Option) and switch.value is SWITCH
    assert isinstance(flee, Option) and flee.value is FLEE


def test_switching_opens_the_team_and_sends_in_the_one_chosen() -> None:
    prompter = Scripted([SWITCH, 2])

    assert _act(prompter) == Action.switch(2)
    _, message, options = prompter.asked[1]
    assert message == "Who comes in instead of Gyarados?"
    assert [o.value for o in options if isinstance(o, Option)] == [0, 1, 2, BACK]


def test_back_returns_to_the_moves() -> None:
    prompter = Scripted([SWITCH, BACK, Action.move(2)])

    assert _act(prompter) == Action.move(2)
    assert prompter.messages().count("What will Gyarados do?") == 2


def test_a_replacement_offers_the_team_without_a_way_back() -> None:
    env = BattleEnvironment(TEAM, ["Blissey"], seed=1)
    view = env.observe(0)
    gyarados = view.active(0)
    gyarados.current_hp = 0
    view.set_pokemon(0, 0, gyarados)
    prompter = Scripted([1])

    assert HumanPlayer(prompter, lambda v: None).choose_replacement(0, view, [1, 2]) == 1

    _, message, options = prompter.asked[0]
    assert message == "Who comes in?"
    assert [o.value for o in options if isinstance(o, Option)] == [0, 1, 2, FLEE]


def test_without_a_pokemon_to_switch_to_there_is_no_switch_entry() -> None:
    env = BattleEnvironment(["Gyarados"], ["Blissey"], seed=1)
    prompter = Scripted([Action.move(0)])

    HumanPlayer(prompter, lambda v: None).choose_action(0, env.observe(0), env.legal_actions(0))

    options = prompter.asked[0][2]
    assert SWITCH not in [o.value for o in options if isinstance(o, Option)]


def test_fleeing_asks_to_confirm_then_abandons_the_battle() -> None:
    with pytest.raises(Fled):
        _act(Scripted([FLEE, True]))

    prompter = Scripted([FLEE, False, Action.move(0)])
    assert _act(prompter) == Action.move(0)
    assert prompter.messages()[1] == "Run away? The battle counts as lost."


def test_one_can_flee_instead_of_sending_a_pokemon_in() -> None:
    env = BattleEnvironment(TEAM, ["Blissey"], seed=1)
    view = env.observe(0)

    with pytest.raises(Fled):
        HumanPlayer(Scripted([FLEE, True]), lambda v: None).choose_replacement(0, view, [1, 2])
