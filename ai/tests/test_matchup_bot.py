from trainerlab.bots import HeuristicBot, MatchupBot
from trainerlab.environment import BattleEnvironment
from trainerlab.harness import run_tournament


def _replacement(team: list[str], opponent: list[str], options: list[int]) -> int:
    """Which of `options` the matchup bot sends in as side 0 against `opponent`'s lead."""
    env = BattleEnvironment(team, opponent, seed=42)
    return MatchupBot().choose_replacement(0, env.observe(0), options)


def test_the_replacement_balances_damage_dealt_and_damage_taken() -> None:
    # Against Dragapult: Weavile takes the least, Hoopa Unbound and Kommo-o hit the hardest, and
    # Kommo-o does both best (deals 240, takes 150).
    team = ["Snorlax", "Weavile", "HoopaUnbound", "KommoO"]

    assert _replacement(team, ["Dragapult"], [1, 2, 3]) == 3


def test_a_pokemon_that_hits_hard_but_takes_more_is_not_preferred() -> None:
    # Against Infernape: Gyarados deals the most (240) but takes even more (300); Dragapult,
    # immune to Close Combat, deals 135 and takes 90.
    team = ["Snorlax", "Blissey", "Dragapult", "Gyarados"]

    assert _replacement(team, ["Infernape"], [1, 2, 3]) == 2


def test_damage_is_weighed_in_absolute_terms_not_as_a_ratio() -> None:
    # Against Snorlax: Dragapult takes nothing but deals little (75); Kommo-o takes 128 but deals
    # 360. The difference favours Kommo-o; a ratio would pick Dragapult.
    assert _replacement(["Blissey", "Dragapult", "KommoO"], ["Snorlax"], [1, 2]) == 2


def test_only_the_proposed_options_are_considered() -> None:
    # Kommo-o is the best matchup, but it is not offered.
    team = ["Snorlax", "Weavile", "HoopaUnbound", "KommoO"]

    assert _replacement(team, ["Dragapult"], [1, 2]) == 1


def test_the_bot_reads_its_own_side_and_the_opposing_active() -> None:
    # Side 1 faces Dragapult: Kommo-o. Judged against its own lead Infernape, it would be Hoopa.
    team = ["Infernape", "Weavile", "HoopaUnbound", "KommoO"]
    env = BattleEnvironment(["Dragapult"], team, seed=42)

    assert MatchupBot().choose_replacement(1, env.observe(1), [1, 2, 3]) == 3


def test_the_matchup_bot_beats_the_damage_only_bot() -> None:
    result = run_tournament(lambda seed: MatchupBot(), lambda seed: HeuristicBot(), pairs=200)
    low, _ = result.interval

    assert low > 0.5
