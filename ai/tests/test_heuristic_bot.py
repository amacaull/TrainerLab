from teams import TEAM0, TEAM1

from trainerlab._engine import Action
from trainerlab.bots import HeuristicBot, RandomBot
from trainerlab.environment import BattleEnvironment
from trainerlab.harness import run_tournament


def _choice(attacker: str, defender: str) -> Action:
    """What the heuristic bot plays as side 0 with `attacker` against `defender`."""
    env = BattleEnvironment([attacker], [defender], seed=42)
    return HeuristicBot().choose_action(0, env.observe(0), env.legal_actions(0))


def test_a_super_effective_move_beats_a_stronger_neutral_one() -> None:
    # Dragapult on Kommo-o: Dragon Darts (Dragon, x2) over Phantom Force (Ghost, more power).
    assert _choice("Dragapult", "KommoO") == Action.move(0)


def test_a_move_the_defender_is_immune_to_is_never_chosen() -> None:
    # Excadrill on Gyarados: Earthquake is its strongest move, but Flying is immune to Ground.
    # Stone Edge (Rock, neutral on Water/Flying) is left as the best.
    assert _choice("Excadrill", "Gyarados") == Action.move(2)


def test_the_same_type_bonus_tips_the_balance() -> None:
    # Gyarados on Snorlax: Waterfall (80, Water, STAB) over Earthquake (100, no STAB).
    assert _choice("Gyarados", "Snorlax") == Action.move(1)


def test_accuracy_lowers_a_move_score() -> None:
    # Starmie on Snorlax: Psychic (90, 100 %) over Hydro Pump (110, 80 %), both with STAB.
    assert _choice("Starmie", "Snorlax") == Action.move(1)


def test_a_move_that_never_misses_keeps_its_full_score() -> None:
    # Hoopa Unbound on Blissey: Aura Sphere (Fighting, x2, never misses) over its STAB moves.
    assert _choice("HoopaUnbound", "Blissey") == Action.move(2)


def test_the_bot_reads_its_own_side_and_the_opposing_active() -> None:
    # Excadrill on Gyarados again, played from side 1: Stone Edge.
    env = BattleEnvironment(["Gyarados"], ["Excadrill"], seed=42)

    action = HeuristicBot().choose_action(1, env.observe(1), env.legal_actions(1))

    assert action == Action.move(2)


def test_the_bot_takes_the_first_proposed_replacement() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["KommoO", "Blissey"], seed=42)

    assert HeuristicBot().choose_replacement(1, env.observe(1), [1, 0]) == 1


def test_the_heuristic_bot_beats_the_random_bot() -> None:
    # Guards the README claim with fewer pairs, to keep the suite fast.
    result = run_tournament(
        lambda seed: HeuristicBot(),
        lambda seed: RandomBot(seed=seed),
        team0=TEAM0,
        team1=TEAM1,
        pairs=100,
    )
    low, _ = result.interval

    assert low > 0.5
