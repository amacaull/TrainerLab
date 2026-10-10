import pytest

from trainerlab._engine import Action, ActionError, TeamError
from trainerlab.environment import BattleEnvironment


def test_a_new_battle_is_not_over() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    assert not env.is_over()


def test_each_side_has_legal_actions_at_the_start() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    assert env.legal_actions(0)
    assert env.legal_actions(1)


def test_the_turn_number_advances_after_a_turn() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)
    turn_before = env.turn

    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    assert env.turn == turn_before + 1


def test_playing_a_turn_returns_the_events() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    events = env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    assert events


def test_a_side_sees_both_leads_but_not_the_opposing_bench() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    view = env.observe(1)

    assert not view.pokemon(0, 0).is_empty()
    assert view.pokemon(0, 1).is_empty()
    assert not view.pokemon(1, 0).is_empty()


def test_after_a_switch_the_side_sees_its_new_active() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)
    switch_to_snorlax = Action.switch(1)

    env.play_turn(switch_to_snorlax, env.legal_actions(1)[0])

    assert env.observe(0).active_index[0] == 1


def test_an_illegal_action_is_refused_and_changes_nothing() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)
    view_before = env.observe(0)

    with pytest.raises(ActionError):
        env.play_turn(Action.switch(0), env.legal_actions(1)[0])

    assert env.observe(0) == view_before


def test_creating_a_battle_with_an_invalid_team_is_refused() -> None:
    with pytest.raises(TeamError):
        BattleEnvironment(["Dragapult", "Dragapult"], ["Blissey"], seed=42)
