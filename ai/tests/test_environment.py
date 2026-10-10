from trainerlab.environment import BattleEnvironment


def test_a_new_battle_is_not_over() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    assert not env.is_over()


def test_each_side_has_legal_actions_at_the_start() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    assert env.legal_actions(0)
    assert env.legal_actions(1)
