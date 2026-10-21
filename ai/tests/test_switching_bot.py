from trainerlab._engine import Action
from trainerlab.bots import RandomBot, SwitchingBot
from trainerlab.environment import BattleEnvironment
from trainerlab.harness import run_tournament


def _choice(team: list[str], opponent: list[str]) -> Action:
    """What the switching bot plays as side 0 with `team` against `opponent`."""
    env = BattleEnvironment(team, opponent, seed=42)
    return SwitchingBot().choose_action(0, env.observe(0), env.legal_actions(0))


def test_a_threatened_pokemon_leaves_for_one_that_resists_the_threat() -> None:
    # Infernape's Close Combat is super effective on Snorlax; Dragapult, a Ghost, is immune.
    assert _choice(["Snorlax", "Dragapult"], ["Infernape"]) == Action.switch(1)


def test_the_refuge_is_a_pokemon_that_resists_not_just_any_bench_pokemon() -> None:
    # Blissey, first on the bench, is just as weak to Fighting: Dragapult is chosen.
    assert _choice(["Snorlax", "Blissey", "Dragapult"], ["Infernape"]) == Action.switch(2)


def test_a_bench_pokemon_that_only_takes_neutral_damage_is_no_refuge() -> None:
    # Corviknight takes Fighting at x1 (Flying halves it, Steel doubles it): Snorlax attacks.
    assert _choice(["Snorlax", "Corviknight"], ["Infernape"]) == Action.move(1)


def test_without_a_super_effective_threat_the_bot_attacks() -> None:
    # Gyarados has nothing super effective on Snorlax.
    assert _choice(["Snorlax", "Dragapult"], ["Gyarados"]) == Action.move(1)


def test_a_status_move_is_no_threat_whatever_its_type() -> None:
    # Snorlax cannot hurt Dragapult: Body Slam is Normal, and Curse, Ghost-typed and so
    # "super effective" on paper, is a status move. Dragapult stays and attacks.
    assert _choice(["Dragapult", "Blissey"], ["Snorlax"]) == Action.move(0)


def test_the_threat_is_the_opponent_strongest_move() -> None:
    # Gyarados on Excadrill: Waterfall (x2) is its strongest move, not Ice Fang (x0.5).
    # Toxapex resists Water, so Excadrill leaves.
    assert _choice(["Excadrill", "Toxapex"], ["Gyarados"]) == Action.switch(1)


def test_the_bot_reads_its_own_side_and_the_opposing_active() -> None:
    env = BattleEnvironment(["Infernape"], ["Snorlax", "Dragapult"], seed=42)

    action = SwitchingBot().choose_action(1, env.observe(1), env.legal_actions(1))

    assert action == Action.switch(1)


def test_the_switching_bot_beats_the_random_bot() -> None:
    result = run_tournament(
        lambda seed: SwitchingBot(), lambda seed: RandomBot(seed=seed), pairs=100
    )
    low, _ = result.interval

    assert low > 0.5
