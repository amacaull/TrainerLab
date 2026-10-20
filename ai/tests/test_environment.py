import random

import pytest
from teams import TEAM0, TEAM1

from trainerlab._engine import Action, ActionError, TeamError
from trainerlab.environment import BattleEnvironment

EventRecord = tuple[int, int, int, int, int, int, float, int]


def _event_log(seed: int) -> list[EventRecord]:
    """Snorlax's Body Slam against Blissey's Seismic Toss for two turns, every event recorded."""
    env = BattleEnvironment(["Snorlax"], ["Blissey"], seed=seed)
    body_slam = Action.move(1)
    seismic_toss = Action.move(3)
    log: list[EventRecord] = []
    for _ in range(2):
        events = env.play_turn(body_slam, seismic_toss)
        log += [(e.kind, e.side, e.slot, e.name_id, e.i0, e.i1, e.f0, e.flags) for e in events]
    return log


def test_a_new_battle_is_not_over() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    assert not env.is_over()
    assert env.winner() is None


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


def test_an_unknown_species_is_refused_with_its_name() -> None:
    with pytest.raises(ValueError, match="KomoO"):
        BattleEnvironment(["KomoO"], ["Dragapult"], seed=42)


def test_after_a_knock_out_the_side_must_replace() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["KommoO", "Blissey"], seed=42)

    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    assert env.pending_replacements() == [1]


def test_after_a_knock_out_the_healthy_bench_can_replace() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["KommoO", "Blissey"], seed=42)

    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    assert env.legal_replacements(1) == [1]
    assert env.legal_replacements(0) == []


def test_a_turn_is_refused_while_a_replacement_is_pending() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["KommoO", "Blissey"], seed=42)
    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    with pytest.raises(ActionError) as error:
        env.play_turn(env.legal_actions(0)[0], Action.move(0))

    assert error.value.subcode == "FAINTED"


def test_after_a_replacement_the_new_pokemon_is_active_and_play_resumes() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["KommoO", "Blissey"], seed=42)
    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    env.replace(1, team_index=1)

    assert env.observe(1).active_index[1] == 1
    assert env.pending_replacements() == []
    assert env.legal_actions(1)


def test_when_both_sides_must_replace_the_faster_side_goes_first() -> None:
    env = BattleEnvironment(["MegaBanette", "Snorlax"], ["Excadrill", "Blissey"], seed=42)
    destiny_bond = Action.move(3)
    earthquake = Action.move(0)
    env.play_turn(destiny_bond, earthquake)

    order = env.pending_replacements()

    assert order == [1, 0]


def test_the_slower_side_cannot_replace_first() -> None:
    env = BattleEnvironment(["MegaBanette", "Snorlax"], ["Excadrill", "Blissey"], seed=42)
    destiny_bond = Action.move(3)
    earthquake = Action.move(0)
    env.play_turn(destiny_bond, earthquake)
    view_before = env.observe(0)

    with pytest.raises(RuntimeError, match="first"):
        env.replace(0, team_index=1)

    assert env.observe(0) == view_before


def test_an_invalid_replacement_is_refused_and_changes_nothing() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["KommoO", "Blissey"], seed=42)
    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])
    view_before = env.observe(1)

    with pytest.raises(ActionError) as error:
        env.replace(1, team_index=0)

    assert error.value.subcode == "INVALID_SWITCH"
    assert env.observe(1) == view_before


def test_a_free_replacement_without_a_knock_out_is_refused() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    with pytest.raises(ActionError) as error:
        env.replace(0, team_index=1)

    assert error.value.subcode == "NOT_FAINTED"


def test_when_a_side_has_no_pokemon_left_the_other_side_wins() -> None:
    env = BattleEnvironment(["Dragapult"], ["KommoO"], seed=42)

    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    assert env.is_over()
    assert env.winner() == 0


def test_when_the_other_side_has_no_pokemon_left_side_1_wins() -> None:
    env = BattleEnvironment(["KommoO"], ["Dragapult"], seed=42)

    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    assert env.is_over()
    assert env.winner() == 1


def test_playing_after_the_end_is_refused() -> None:
    env = BattleEnvironment(["Dragapult"], ["KommoO"], seed=42)
    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    with pytest.raises(RuntimeError, match="over"):
        env.play_turn(env.legal_actions(0)[0], Action.move(0))


def test_replacing_after_the_end_is_refused() -> None:
    env = BattleEnvironment(["Dragapult"], ["KommoO"], seed=42)
    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    with pytest.raises(RuntimeError, match="over"):
        env.replace(1, team_index=0)


def test_a_battle_that_reaches_the_turn_limit_is_a_draw() -> None:
    env = BattleEnvironment(["Snorlax"], ["Blissey"], seed=42, max_turns=3)

    for _ in range(3):
        env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])

    assert env.is_over()
    assert env.winner() is None


def test_when_both_sides_lose_their_last_pokemon_together_it_is_a_draw() -> None:
    env = BattleEnvironment(["MegaBanette"], ["Excadrill"], seed=42)
    destiny_bond = Action.move(3)
    earthquake = Action.move(0)

    env.play_turn(destiny_bond, earthquake)

    assert env.is_over()
    assert env.winner() is None


def test_the_same_seed_and_choices_give_the_same_events() -> None:
    first = _event_log(seed=42)
    second = _event_log(seed=42)

    assert first
    assert first == second


def test_different_seeds_give_different_events() -> None:
    first = _event_log(seed=1)
    second = _event_log(seed=2)

    assert first != second


@pytest.mark.parametrize("seed", range(20))
def test_a_full_battle_between_random_players_ends_with_a_result(seed: int) -> None:
    env = BattleEnvironment(TEAM0, TEAM1, seed=seed)
    rng = random.Random(seed)

    while not env.is_over():
        pending = env.pending_replacements()
        if pending:
            for side in pending:
                env.replace(side, team_index=rng.choice(env.legal_replacements(side)))
        else:
            env.play_turn(rng.choice(env.legal_actions(0)), rng.choice(env.legal_actions(1)))

    assert env.winner() in (0, 1, None)
