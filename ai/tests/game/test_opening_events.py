from trainerlab import _engine
from trainerlab.bots.rules import hitter
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment

ABILITY_TRIGGERED, TERRAIN_STARTED, FAINTED, SWITCHED_IN = 13, 30, 2, 12


def test_the_leads_entry_abilities_are_reported() -> None:
    env = BattleEnvironment(["TapuKoko"], ["Snorlax"], seed=1)

    assert [event.kind for event in env.opening_events] == [ABILITY_TRIGGERED, TERRAIN_STARTED]


def test_the_game_loop_reports_every_turn_and_replacement() -> None:
    reported: list[list[_engine.Event]] = []
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["KommoO", "Blissey"], seed=42)

    play_battle(env, hitter(), hitter(), reported.append)

    kinds = [[event.kind for event in events] for events in reported]
    assert FAINTED in kinds[0]  # turn 1: Kommo-o falls
    assert kinds[1][-1] == SWITCHED_IN  # then the replacement, reported on its own
    assert len(reported) == env.turn + 1
