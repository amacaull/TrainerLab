from trainerlab._engine import Action
from trainerlab.bots import OutlookBot, SurvivalBot
from trainerlab.environment import BattleEnvironment


def _choices(team: list[str], foes: list[str], side: int = 0) -> tuple[Action, Action]:
    """What rule 4 and rule 5 play on the first turn, for the side given."""
    env = BattleEnvironment(*((team, foes) if side == 0 else (foes, team)), seed=1)
    view, legal = env.observe(side), env.legal_actions(side)
    return SurvivalBot().choose_action(side, view, legal), OutlookBot().choose_action(
        side, view, legal
    )


def test_fixed_damage_is_no_longer_worth_zero() -> None:
    # Blissey: Soft-Boiled, Wish, Teleport, Seismic Toss. Every power is 0 to rule 4.
    rule_four, rule_five = _choices(["Blissey"], ["Snorlax"])

    assert rule_four == Action.move(0)
    assert rule_five == Action.move(3)


def test_a_move_into_an_ability_immunity_is_avoided() -> None:
    # Excadrill: Earthquake, Iron Head... Earthquake doubles on Poison, Levitate makes it 0.
    rule_four, rule_five = _choices(["Excadrill"], ["WeezingGalar"])

    assert rule_four == Action.move(0)
    assert rule_five == Action.move(1)


def test_the_bot_reads_its_own_side() -> None:
    _, rule_five = _choices(["Blissey"], ["Snorlax"], side=1)

    assert rule_five == Action.move(3)


def test_escapes_are_still_rule_four() -> None:
    # Conkeldurr outspeeds Snorlax and would knock it out from 367 HP: Corviknight comes in.
    env = BattleEnvironment(
        ["Snorlax", "Azumarill", "Corviknight", "GiratinaOrigin"], ["Conkeldurr"], seed=1
    )
    view = env.observe(0)
    snorlax = view.pokemon(0, 0)
    snorlax.current_hp = 367
    view.set_pokemon(0, 0, snorlax)

    assert OutlookBot().choose_action(0, view, env.legal_actions(0)) == Action.switch(2)
