from trainerlab.environment import BattleEnvironment


def test_a_new_battle_is_not_over() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["Blissey"], seed=42)

    assert not env.is_over()
