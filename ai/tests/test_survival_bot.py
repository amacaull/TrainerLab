from trainerlab._engine import Action
from trainerlab.bots import HeuristicBot, RandomBot, SurvivalBot
from trainerlab.environment import BattleEnvironment
from trainerlab.harness import run_tournament

PARALYSIS = 4

# Conkeldurr outspeeds Snorlax and should hit it for about 444 of its 524 HP. On the bench,
# Azumarill and Corviknight would take a cheap hit; Corviknight has the better matchup.
TEAM = ["Snorlax", "Azumarill", "Corviknight", "GiratinaOrigin"]


def _decide(team: list[str], active_hp: int, bench_hp: dict[int, int] | None = None) -> Action:
    env = BattleEnvironment(team, ["Conkeldurr"], seed=1)
    view = env.observe(0)
    for index, hp in {0: active_hp, **(bench_hp or {})}.items():
        pokemon = view.pokemon(0, index)
        pokemon.current_hp = hp
        view.set_pokemon(0, index, pokemon)
    return SurvivalBot().choose_action(0, view, env.legal_actions(0))


def _rule_one(team: list[str]) -> Action:
    env = BattleEnvironment(team, ["Conkeldurr"], seed=1)
    return HeuristicBot().choose_action(0, env.observe(0), env.legal_actions(0))


def test_a_pokemon_about_to_fall_first_escapes_to_the_best_survivor() -> None:
    assert _decide(TEAM, active_hp=367) == Action.switch(2)


def test_a_pokemon_that_survives_the_hit_stays_and_attacks() -> None:
    assert _decide(TEAM, active_hp=524) == _rule_one(TEAM)


def test_half_its_hp_is_the_threshold_for_being_worth_saving() -> None:
    assert _decide(TEAM, active_hp=262) == Action.switch(2)  # exactly 50 %
    assert _decide(TEAM, active_hp=261) == _rule_one(TEAM)  # just below: left to faint


def test_a_refuge_may_lose_at_most_a_third_of_its_hp() -> None:
    # Corviknight should take 110.04: a third of 331 HP covers it, a third of 330 does not.
    team = ["Snorlax", "Corviknight"]

    assert _decide(team, active_hp=367, bench_hp={1: 331}) == Action.switch(1)
    assert _decide(team, active_hp=367, bench_hp={1: 330}) == _rule_one(team)


def test_an_opponent_that_does_not_outspeed_is_no_reason_to_leave() -> None:
    # Paralysis halves Conkeldurr's speed below Snorlax's.
    env = BattleEnvironment(TEAM, ["Conkeldurr"], seed=1)
    view = env.observe(0)
    snorlax, conkeldurr = view.pokemon(0, 0), view.pokemon(1, 0)
    snorlax.current_hp = 367
    conkeldurr.status = PARALYSIS
    view.set_pokemon(0, 0, snorlax)
    view.set_pokemon(1, 0, conkeldurr)

    assert SurvivalBot().choose_action(0, view, env.legal_actions(0)) == _rule_one(TEAM)


def test_the_bot_reads_its_own_side_and_the_opposing_active() -> None:
    env = BattleEnvironment(["Conkeldurr"], TEAM, seed=1)
    view = env.observe(1)
    snorlax = view.pokemon(1, 0)
    snorlax.current_hp = 367
    view.set_pokemon(1, 0, snorlax)

    assert SurvivalBot().choose_action(1, view, env.legal_actions(1)) == Action.switch(2)


def test_the_survival_bot_beats_the_random_bot() -> None:
    result = run_tournament(
        lambda seed: SurvivalBot(), lambda seed: RandomBot(seed=seed), pairs=100
    )
    low, _ = result.interval

    assert low > 0.5


def test_replacements_still_follow_rule_three() -> None:
    env = BattleEnvironment(
        ["Dragapult"], ["Snorlax", "Weavile", "HoopaUnbound", "KommoO"], seed=42
    )

    assert SurvivalBot().choose_replacement(1, env.observe(1), [1, 2, 3]) == 3
